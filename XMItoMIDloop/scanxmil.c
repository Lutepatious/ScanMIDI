#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>

const unsigned char midiheader[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1 };
const unsigned char midiTrheader[] = { 'M', 'T', 'r', 'k' };

// below 2 delta (1 byte) + midi control (0x80 - 0xFF)
const unsigned char smf_gs_reset[] = {0x00, 0xF0, 0x0A, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
unsigned char smf_sc55_mode[] = {  0x01, 0xB0, 0x20, 0x01 }; // LSB only

#define DEFAULT_TEMPO 120UL // MIDI tempo default (/minute)
#define XMI_FREQ 120UL // XMI Frequency (/second)
#define DEFAULT_TIMEBASE (XMI_FREQ*60UL/DEFAULT_TEMPO) // Must be 60
#define DEFAULT_QN (60UL * 1000000UL / DEFAULT_TEMPO) // Must be 500000 (microseconds)

unsigned short timebase = 960; // ideal timebase 960
unsigned qnlen = DEFAULT_QN; // quarter note length

#define PUT_DELTA(ptr, _delta) { \
	unsigned _tdelta = (_delta); \
	unsigned _tdelay = _tdelta & 0x7F; \
	while ((_tdelta >>= 7)) { \
		_tdelay <<= 8; \
		_tdelay |= (_tdelta & 0x7F) | 0x80; \
	} \
	while (1) { \
		*(ptr)++ = _tdelay & 0xFF; \
		if (_tdelay & 0x80) { \
			_tdelay >>= 8; \
		} else { \
			break; \
		} \
	} \
}

#define COPY_DATA(dst, src, num) { \
	memcpy_s(dst, num, src, num); \
	dst += num; \
	src += num; \
}

struct NOEVENTS {
	unsigned delta;
	unsigned char off[3];
} off_events[1000] = { { 0xFFFFFFFFL,{ 0, 0, 0 } } };

struct BRANCHES {
	unsigned short id;
	unsigned dest;
} branch[16];

// max_velocity to 0;
#define PUT_NOEVENT(dst, num) { \
	*(dst)++ = off_events[(num)].off[0]; \
	*(dst)++ = off_events[(num)].off[1]; \
	*(dst)++ = 0; \
}

int comp_events(struct NOEVENTS *a, struct NOEVENTS *b)
{
	if (a->delta < b->delta) {
		return -1;
	}
	else if (a->delta > b->delta) {
		return 1;
	}
	else {
		return 0;
	}

}


int main(int argc, char **argv)
{
	FILE *pFi, *pFo;

	if (argc != 2) {
		fprintf(stderr, "Usage:%s infile\n", argv[0]);
		exit(-1);
	}

	if (fopen_s(&pFi, argv[1], "rb")) {
		fprintf(stderr, "File %s cannot open\n", argv[1]);
		exit(errno);
	}

	fseek(pFi, 0, SEEK_END);
	unsigned char *midi_data;
	long fsize = ftell(pFi);
	fseek(pFi, 0, SEEK_SET);

//	printf("DEBUG: fsize %ld bytes\n", fsize);
	if (NULL == (midi_data = malloc(fsize))) {
		fprintf(stderr, "Memory allocation error\n");
		fclose(pFi);
		exit(errno);
	}

	if (fsize != fread_s(midi_data, fsize, sizeof(signed char), fsize, pFi)) {
		fprintf(stderr, "File read error\n");
		fclose(pFi);
		exit(errno);
	}
	fclose(pFi);
//	printf("DEBUG: file read complete goto on-memory\n");

	// pass 1 Analyze xmi header
	unsigned char *cur = midi_data;
	if (memcmp(cur, "FORM", 4)) {
		fprintf(stderr, "Not XMIDI file (FORM)\n");
	}
	cur += 4;

	unsigned lFORM = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	if (memcmp(cur, "XDIR", 4)) {
		fprintf(stderr, "Not XMIDI file (XDIR)\n");
	}
	cur += 4;

	if (memcmp(cur, "INFO", 4)) {
		fprintf(stderr, "Not XMIDI file (INFO)\n");
	}
	cur += 4;

	unsigned lINFO = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	unsigned short seqCount = *((unsigned short *)cur);
	cur += 2;

//	printf("seqCount: %d\n", seqCount);

	if (memcmp(cur, "CAT ", 4)) {
		fprintf(stderr, "Not XMIDI file (CAT )\n");
	}
	cur += 4;

	unsigned lCAT = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	if (memcmp(cur, "XMID", 4)) {
		fprintf(stderr, "Not XMIDI file (XMID)\n");
	}
	cur += 4;

	if (memcmp(cur, "FORM", 4)) {
		fprintf(stderr, "Not XMIDI file (FORM)\n");
	}
	cur += 4;

	unsigned lFORM2 = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	if (memcmp(cur, "XMID", 4)) {
		fprintf(stderr, "Not XMIDI file (XMID)\n");
	}
	cur += 4;

	if (memcmp(cur, "TIMB", 4)) {
		fprintf(stderr, "Not XMIDI file (TIMB)\n");
	}
	cur += 4;

	unsigned lTIMB = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;

	for (unsigned i = 0; i < lTIMB; i += 2) {
//		printf("patch@bank: %3d@%3d\n", *cur, *(cur + 1));
		cur += 2;
	}

	if (!memcmp(cur, "RBRN", 4)) {
		cur += 4;
//		printf("(RBRN)\n");
		unsigned lRBRN = _byteswap_ulong(*((unsigned *)cur));
		cur += 4;

		unsigned short nBranch = *((unsigned short *)cur);
		cur += 2;

		for (unsigned i = 0; i < nBranch; i++) {
			branch[i].id = *(unsigned short *)cur;
			cur += 2;
			branch[i].dest = *(unsigned *)cur;
			cur += 4;
//			printf("id/dest: %04X@%08X\n", branch[i].id, branch[i].dest);
		}
	}

	if (memcmp(cur, "EVNT", 4)) {
		fprintf(stderr, "Not XMIDI file (EVNT)\n");
	}
	cur += 4;

	unsigned lEVNT = _byteswap_ulong(*((unsigned *)cur));
	cur += 4;
//	printf("whole event length: %d\n", lEVNT);


	// pass 2 Simple decode
	unsigned char *midi_decode;

	if (NULL == (midi_decode = malloc(fsize * 4 + 189))) {
		fprintf(stderr, "Memory (decode buffer) allocation error\n");
		exit(errno);
	}
	unsigned char *dcur = midi_decode;

	unsigned char *st = cur;
	unsigned oevents = 0;
	unsigned on_loop = 0;
	unsigned char *loopback = st;
	unsigned delay = 0;

	memcpy_s(dcur, sizeof(smf_gs_reset), smf_gs_reset, sizeof(smf_gs_reset));
	dcur += sizeof(smf_gs_reset);
	delay = DEFAULT_TIMEBASE * 4;

	for (int ch = 0; ch < 16; ch++) {
		memcpy_s(dcur, sizeof(smf_sc55_mode), smf_sc55_mode, sizeof(smf_sc55_mode));
		smf_sc55_mode[1]++;

		dcur += sizeof(smf_sc55_mode);
		delay -= 1;
	}

	while (cur - st < lEVNT) {
//		printf("%6d:", cur - st);

		if (*cur < 0x80) {
			while (*cur == 0x7F) {
				delay += *cur++;
			}
			if (*cur < 0x80) {
				delay += *cur++;
			}
//			printf("delay:%d\n", delay);

			while (delay >= off_events[0].delta) {
//				for (unsigned i = 0; i < oevents; i++) {
//					printf("event %d d=%d:%02X:%02X:%02X\n", i, off_events[i].delta, off_events[i].off[0], off_events[i].off[1], off_events[i].off[2]);
//				}
				PUT_DELTA(dcur, off_events[0].delta);
				PUT_NOEVENT(dcur, 0);
				delay -= off_events[0].delta;
				for (unsigned i = 1; i < oevents; i++) {
					off_events[i].delta -= off_events[0].delta;
				}
				off_events[0].delta = 0xFFFFFFFFL;

				qsort(off_events, oevents, sizeof(struct NOEVENTS), (int(*)(const void*, const void*))comp_events);

				oevents--;
			}
			for (unsigned i = 0; i < oevents; i++) {
				off_events[i].delta -= delay;
//				printf("event %d d=%d:%02X:%02X:%02X\n", i, off_events[i].delta, off_events[i].off[0], off_events[i].off[1], off_events[i].off[2]);
			}

		}
		else {
			switch (*cur & 0xF0) {
			case 0xA0:
			case 0xE0:
				PUT_DELTA(dcur, delay);
				delay = 0;
				COPY_DATA(dcur, cur, 3);
				break;
			case 0xC0:
			case 0xD0:
				PUT_DELTA(dcur, delay);
				delay = 0;
				COPY_DATA(dcur, cur, 2);
				break;
			case 0x90:
				PUT_DELTA(dcur, delay);
				delay = 0;
				COPY_DATA(dcur, cur, 3);
				unsigned delta = 0;

				while (*cur & 0x80) {
					delta += *cur++ & 0x7F;
					delta <<= 7;
				}
				delta += *cur++ & 0x7F;

				off_events[oevents].delta = delta;
				off_events[oevents].off[0] = *(dcur - 3);
				off_events[oevents].off[1] = *(dcur - 2);

				oevents++;

				qsort(off_events, oevents, sizeof(struct NOEVENTS), (int(*)(const void*, const void*))comp_events);
				break;
			case 0xB0:
				switch (*(cur + 1)) {
				case 32:
				case 33:
				case 34:
				case 35:
				case 36:
				case 37:
				case 38:
				case 39:
				case 40:
				case 41:
				case 42:
				case 43:
				case 44:
				case 45:
				case 46:
				case 58:
				case 59:
				case 60:
				case 61:
				case 62:
				case 63:
				case 110:
				case 111:
				case 112:
				case 113:
				case 114:
				case 115:
				case 116:
				case 117:
				case 118:
				case 119:
				case 120:
//					printf("XMI special control %d\n", *(cur + 1));
					cur += 3;
					continue;
				default:
					PUT_DELTA(dcur, delay);
					delay = 0;
					COPY_DATA(dcur, cur, 3);
				}
				break;
			case 0xF0:
				if (*cur == 0xFF) {
//					printf("META\n");
					if (*(cur + 1) == 0x2F) {
						goto track_ends;
					}

					PUT_DELTA(dcur, delay);
					delay = 0;
					COPY_DATA(dcur, cur, 2);
					unsigned textlen = *cur + 1;
					COPY_DATA(dcur, cur, textlen);
				}
				else {
					printf("wrong event\n");
					exit(-1);
				}
				break;
			default:
				printf("wrong event\n");
				exit(-1);
			}
		}
	}

track_ends:;
	if (oevents)
		printf("flush %3d note offs\n", oevents);
	while (oevents) {
		PUT_DELTA(dcur, off_events[0].delta + delay);
		delay = 0;
		PUT_NOEVENT(dcur, 0);
		for (unsigned i = 1; i < oevents; i++) {
			off_events[i].delta -= off_events[0].delta;
		}
		off_events[0].delta = 0xFFFFFFFFL;
		qsort(off_events, oevents, sizeof(struct NOEVENTS), (int(*)(const void*, const void*))comp_events);
		oevents--;
	}

	*dcur++ = 0x00;
	*dcur++ = 0xFF;
	*dcur++ = 0x2F;
	*dcur++ = 0x00;

	unsigned dlen = dcur - midi_decode;

#define CHANGE_DELTA 1
	// pass 3 Apply Tempo & Timebase
	unsigned char *midi_write;

	if (NULL == (midi_write = malloc(fsize * 4))) {
		fprintf(stderr, "Memory (write buffer) allocation error\n");
		exit(errno);
	}
	unsigned char *tcur = midi_write;
	unsigned char *pos = midi_decode;
	unsigned __int64 total_time = 0;
	unsigned __int64 total_time_changed = 0;

	while (pos < dcur) {
		// first delta-time
		unsigned __int64 delta = 0;
		while (*pos & 0x80) {
			delta += *pos++ & 0x7F;
			delta <<= 7;
		}
		delta += *pos++ & 0x7F;
		total_time += delta;

#if CHANGE_DELTA
		// change delta here!!
		// For avoid time base error. Do not change delta directly. Calcurate total time and re-build new delta from the total.
		unsigned __int64 total_time_changed_c = (total_time * timebase * DEFAULT_QN * 2) / ((unsigned __int64)qnlen * DEFAULT_TIMEBASE) + 1;
		total_time_changed_c >>= 1;
		delta = total_time_changed_c - total_time_changed;
		total_time_changed = total_time_changed_c;
#endif

		PUT_DELTA(tcur, delta);
		// last -  event
		switch (*pos & 0xF0) {
		case 0x80:
		case 0x90:
		case 0xA0:
		case 0xB0:
		case 0xE0:
			COPY_DATA(tcur, pos, 3);
			break;
		case 0xC0:
		case 0xD0:
			COPY_DATA(tcur, pos, 2);
			break;
		case 0xF0:
			if (0xFF == *pos) {
				if (0x51 == *(pos + 1)) {
					COPY_DATA(tcur, pos, 3);
					total_time_changed *= qnlen * 2;
					qnlen = (*(unsigned char *)(pos) << 16) + (*(unsigned char *)(pos + 1) << 8) + *(unsigned char *)(pos + 2);
					total_time_changed /= qnlen;
					total_time_changed++;
					total_time_changed >>= 1;

#if !(CHANGE_DELTA)
					unsigned new_qnlen = timebase * DEFAULT_QN * 2 / DEFAULT_TIMEBASE;
					if (new_qnlen & 0x1) {
						new_qnlen++;
					}
					new_qnlen >>= 1;
					printf("qnlen %ld ==> new_qnlen %ld\n", qnlen, new_qnlen);
					*(unsigned char *)(pos + 2) = new_qnlen & 0xff;
					*(unsigned char *)(pos + 1) = (new_qnlen >> 8) & 0xff;
					*(unsigned char *)(pos) = (new_qnlen >> 16) & 0xff;
#endif
					COPY_DATA(tcur, pos, 3);
				}
				else {
					COPY_DATA(tcur, pos, 2);
					unsigned textlen = *pos;
					COPY_DATA(tcur, pos, 1 + textlen);
				}
			}
			else if (0xF0 == *pos) {
				COPY_DATA(tcur, pos, *(pos+1) + 2);
			}
			else {
				printf("Bad event B %02x %02x %02x at %04x\n", *pos, *(pos+1), *(pos+2), pos - midi_decode);
				exit(-1);
			}
			break;
		default:
			printf("Bad event A %02x %02x %02x at %04x\n", *pos, *(pos + 1), *(pos + 2), pos - midi_decode);
			exit(-1);
		}
	}
	unsigned tlen = tcur - midi_write;

	//	printf("%7d\n", tlen);
	unsigned char pt[_MAX_PATH], fn[_MAX_FNAME];

	_splitpath_s(argv[1], NULL, 0, NULL, 0, fn, _MAX_FNAME, NULL, 0);
	_makepath_s(pt, _MAX_PATH, NULL, NULL, fn, ".mid");

	// output
	if (fopen_s(&pFo, pt, "wb")) {
		fprintf(stderr, "File %s cannot open\n", pt);
		exit(errno);
	}

	// Form 0 write
	if (12 != fwrite(midiheader, sizeof(unsigned char), 12, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}
	unsigned short mh_timebase = _byteswap_ushort(timebase);

	if (1 != fwrite(&mh_timebase, sizeof(unsigned short), 1, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}

	if (4 != fwrite(midiTrheader, sizeof(unsigned char), 4, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}

	unsigned bs_tlen = _byteswap_ulong(tlen);
	if (1 != fwrite(&bs_tlen, sizeof(unsigned), 1, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}

	if (tlen != fwrite(midi_write, sizeof(unsigned char), tlen, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}
	fclose(pFo);
}