/* USBSYNC.EXE - Windows 3.1x: write all cached data to the USB drives, so
 * that they can be unplugged (like USBSYNC.COM for DOS).
 *
 *   USBSYNC [/E] [/Q] [/X]
 *     /E  also stop (eject) the drives
 *     /Q  show a message only on errors
 *     /X  exit Windows afterwards (tests)
 *   test options: /W:file write a 32 KB test file first, /N no sync,
 *     /P switch QEMU off at the end (isa-debug-exit at port F4h), like
 *     unplugging without the flush Windows does when it exits
 *
 * 1. DOS buffers and the 32-bit file access cache (VCACHE) of Windows for
 *    Workgroups 3.11: INT 21h AH=0Dh
 * 2. SMARTDRV: INT 2Fh AX=4A10h BX=0001h (in real mode, DPMI 0300h)
 * 3. the drives' write caches: SYNCHRONIZE CACHE(10) to the devices of
 *    EUSBASPI through the DOS ASPI manager (SCSIMGR$), called in real mode
 *    with DPMI 0301h through a small thunk in DOS memory
 *
 * Copyright (C) 2026 GeosOne.  GNU General Public License version 3. */

#include <windows.h>
#include <string.h>
#include <stdio.h>

/* DPMI real mode call structure */
struct rmregs {
	unsigned long edi, esi, ebp, res, ebx, edx, ecx, eax;
	unsigned short flags, es, ds, fs, gs, ip, cs, sp, ss;
};

/* layout of the DOS memory block */
#define B_THUNK  0x00    /* push cs; push B_SRB; call far [cs:B_ENTRY]; add sp,4; retf */
#define B_ENTRY  0x10
#define B_NAME   0x20
#define B_SRB    0x30
#define B_BUF    0xB0
#define B_SIZE   0x100

/* SRB (ASPI for DOS) */
#define SRB_Cmd        0x00
#define SRB_Status     0x01
#define SRB_HaId       0x02
#define SRB_Flags      0x03
#define HA_Count       0x08
#define HA_SCSI_ID     0x09
#define HA_ManagerId   0x0A
#define SRB_Target     0x08
#define SRB_DeviceType 0x0A
#define SRB_BufLen     0x0A
#define SRB_SenseLen   0x0E
#define SRB_BufPointer 0x0F
#define SRB_CDBLen     0x17
#define SRB_TargStat   0x19
#define SRB_CDB        0x40
#define SRB_SIZE       (SRB_CDB + 16 + 32)

static unsigned short rmseg;           /* real mode segment of the block */
static unsigned char far *blk;         /* the block in protected mode */
static int opt_eject, opt_quiet, opt_exit, opt_nosync, opt_poweroff;
static char testfile[80];

static int dpmi_rm_int( int intno, struct rmregs far *r )
{
	int fail = 0;
	_asm {
		push di
		mov ax, 0300h
		mov bl, byte ptr intno
		xor bh, bh
		xor cx, cx
		les di, r
		int 31h
		sbb ax, ax
		mov fail, ax
		pop di
	}
	return fail;
}

static int dpmi_rm_call( struct rmregs far *r )
{
	int fail = 0;
	_asm {
		push di
		mov ax, 0301h
		xor bh, bh
		xor cx, cx
		les di, r
		int 31h
		sbb ax, ax
		mov fail, ax
		pop di
	}
	return fail;
}

static void rm_clear( struct rmregs *r )
{
	memset( r, 0, sizeof(*r) );
}

/* INT 21h in real mode; returns -1 if CF */
static int rm_dos( unsigned ax, unsigned bx, unsigned cx, unsigned dx, unsigned *ax_out )
{
	struct rmregs r;
	rm_clear( &r );
	r.eax = ax; r.ebx = bx; r.ecx = cx; r.edx = dx;
	r.ds = rmseg;
	if ( dpmi_rm_int( 0x21, &r ) || ( r.flags & 1 ) )
		return -1;
	if ( ax_out )
		*ax_out = (unsigned)r.eax;
	return 0;
}

static void call_aspi( void )
{
	struct rmregs r;
	rm_clear( &r );
	r.cs = rmseg;
	r.ip = B_THUNK;
	dpmi_rm_call( &r );
}

static void clear_srb( void )
{
	_fmemset( blk + B_SRB, 0, SRB_SIZE );
}

static unsigned char srb( int ofs ) { return blk[B_SRB + ofs]; }

/* SCSI command without data (len = 0) or data in (to B_BUF) */
static int scsi( int ha, int target, const unsigned char *cdb, int cdblen, int len )
{
	clear_srb();
	blk[B_SRB + SRB_Cmd] = 2;
	blk[B_SRB + SRB_HaId] = ha;
	blk[B_SRB + SRB_Flags] = len ? 0x08 : 0x18;
	blk[B_SRB + SRB_Target] = target;
	*(unsigned long far *)( blk + B_SRB + SRB_BufLen ) = len;
	*(unsigned short far *)( blk + B_SRB + SRB_BufPointer ) = B_BUF;
	*(unsigned short far *)( blk + B_SRB + SRB_BufPointer + 2 ) = rmseg;
	blk[B_SRB + SRB_SenseLen] = 18;
	blk[B_SRB + SRB_CDBLen] = cdblen;
	_fmemcpy( blk + B_SRB + SRB_CDB, cdb, cdblen );
	call_aspi();
	return srb( SRB_Status );
}

int PASCAL WinMain( HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show )
{
	static const unsigned char thunk[] = {
		0x0E, 0x68, B_SRB, 0x00, 0x2E, 0xFF, 0x1E, B_ENTRY, 0x00, 0x83, 0xC4, 0x04, 0xCB };
	static const unsigned char cdb_inq[6] = { 0x12, 0, 0, 0, 36, 0 };
	static const unsigned char cdb_sync[10] = { 0x35, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	static const unsigned char cdb_allow[6] = { 0x1E, 0, 0, 0, 0, 0 };
	static const unsigned char cdb_stop[6] = { 0x1B, 0, 0, 0, 0x02, 0 };
	static char text[1024];
	unsigned long mem;
	unsigned handle, ax;
	int ha, nha, target, haid, drives = 0, errors = 0, smartdrv = 0;
	struct rmregs r;
	LPSTR p;

	(void)inst; (void)prev; (void)show;
	for ( p = cmd; *p; p++ )
		if ( *p == '/' || *p == '-' ) {
			char c = p[1] & 0xDF;
			if ( c == 'E' ) opt_eject = 1;
			if ( c == 'Q' ) opt_quiet = 1;
			if ( c == 'X' ) opt_exit = 1;
			if ( c == 'N' ) opt_nosync = 1;
			if ( c == 'P' ) opt_poweroff = 1;
			if ( c == 'W' && p[2] == ':' ) {
				int i = 0;
				for ( p += 3; *p && *p != ' ' && i < 79; p++ )
					testfile[i++] = *p;
				testfile[i] = 0;
				p--;
			}
		}
	if ( testfile[0] ) {   /* test: a file written through Windows */
		static char data[1024];
		HFILE f = _lcreat( testfile, 0 );
		int i;
		for ( i = 0; i < sizeof(data); i++ )
			data[i] = 'A' + i % 26;
		if ( f != HFILE_ERROR ) {
			for ( i = 0; i < 32; i++ )
				_lwrite( f, data, sizeof(data) );
			_lclose( f );
		}
		if ( opt_nosync )
			goto poweroff;
	}

	/* 1. DOS buffers, 32-bit file access */
	_asm {
		mov ah, 0Dh
		int 21h
	}

	/* 2. SMARTDRV */
	rm_clear( &r );
	r.eax = 0x4A10;
	if ( !dpmi_rm_int( 0x2F, &r ) && (unsigned)r.eax == 0xBABE ) {
		rm_clear( &r );
		r.eax = 0x4A10;
		r.ebx = 1;
		dpmi_rm_int( 0x2F, &r );
		smartdrv = 1;
	}

	/* 3. the drives */
	strcpy( text, smartdrv ? "SMARTDRV cache written.\n\n" : "" );
	if ( !( mem = GlobalDosAlloc( B_SIZE ) ) ) {
		MessageBox( NULL, "No DOS memory.", "USBSYNC", MB_OK | MB_ICONSTOP );
		return 1;
	}
	rmseg = HIWORD( mem );
	blk = (unsigned char far *)MAKELP( LOWORD( mem ), 0 );
	_fmemset( blk, 0, B_SIZE );
	_fmemcpy( blk + B_THUNK, thunk, sizeof(thunk) );
	_fstrcpy( (char far *)( blk + B_NAME ), "SCSIMGR$" );
	if ( rm_dos( 0x3D00, 0, 0, B_NAME, &handle ) ) {
		strcat( text, "No ASPI manager (SCSIMGR$) loaded." );
		errors++;
		goto done;
	}
	rm_dos( 0x4402, handle, 4, B_ENTRY, &ax );
	rm_dos( 0x3E00, handle, 0, 0, NULL );

	clear_srb();
	call_aspi();                          /* host adapter inquiry */
	nha = srb( HA_Count );
	for ( ha = 0; ha < nha; ha++ ) {
		clear_srb();
		blk[B_SRB + SRB_HaId] = ha;
		call_aspi();
		haid = srb( HA_SCSI_ID );
		if ( _fmemcmp( blk + B_SRB + HA_ManagerId, "EUSBASPI", 8 ) )
			continue;                     /* not an adapter of EUSBASPI */
		for ( target = 0; target < 8; target++ ) {
			int st, type;
			char line[100];
			if ( target == haid )
				continue;
			clear_srb();
			blk[B_SRB + SRB_Cmd] = 1;     /* get device type */
			blk[B_SRB + SRB_HaId] = ha;
			blk[B_SRB + SRB_Target] = target;
			call_aspi();
			if ( srb( SRB_Status ) != 1 )
				continue;
			type = srb( SRB_DeviceType ) & 0x1F;
			if ( type != 0x00 && type != 0x07 && type != 0x0E )
				continue;
			drives++;
			_fmemset( blk + B_BUF, ' ', 36 );
			scsi( ha, target, cdb_inq, 6, 36 );
			{   /* vendor and product without the padding */
				char v[9], pr[17];
				int n;
				_fmemcpy( v, blk + B_BUF + 8, 8 );
				for ( n = 8; n && v[n - 1] == ' '; n-- );
				v[n] = 0;
				_fmemcpy( pr, blk + B_BUF + 16, 16 );
				for ( n = 16; n && pr[n - 1] == ' '; n-- );
				pr[n] = 0;
				sprintf( line, "%s %s: ", v, pr );
			}
			strcat( text, line );
			st = scsi( ha, target, cdb_sync, 10, 0 );
			if ( st == 1 )
				strcat( text, "synchronized" );
			else if ( srb( SRB_TargStat ) == 2 && ( blk[B_SRB + SRB_CDB + 10 + 2] & 0x0F ) == 5 )
				strcat( text, "no write cache" );   /* ILLEGAL REQUEST */
			else {
				strcat( text, "ERROR" );
				errors++;
			}
			if ( opt_eject ) {
				scsi( ha, target, cdb_allow, 6, 0 );
				scsi( ha, target, cdb_stop, 6, 0 );
				strcat( text, ", stopped" );
			}
			strcat( text, "\n" );
		}
	}
	_asm {
		mov ah, 0Dh
		int 21h
	}
	if ( !drives )
		strcat( text, "No USB drives found (the DOS buffers were written)." );
	else if ( !errors )
		strcat( text, opt_eject ? "\nAll data written and the drives stopped: unplug them now."
		                        : "\nAll data written: the USB drives can be unplugged now." );
	else
		strcat( text, "\nA drive did not confirm the data: do not unplug it yet." );
done:
	GlobalDosFree( LOWORD( mem ) );
	if ( !opt_quiet || errors )
		MessageBox( NULL, text, "USB drives", MB_OK | MB_TASKMODAL | ( errors ? MB_ICONSTOP : MB_ICONINFORMATION ) );
poweroff:
	if ( opt_poweroff )
		_asm {
			mov al, 0
			out 0F4h, al
		}
	if ( opt_exit )
		ExitWindows( 0, 0 );
	return errors ? 2 : 0;
}
