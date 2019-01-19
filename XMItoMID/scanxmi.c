#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>

unsigned char midiheader[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1 };
unsigned char midiTrheader[] = { 'M', 'T', 'r', 'k' };
unsigned char midiheaderf1[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };

#define FORMAT_0 

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

#define COPY_DATA_1(dst, src) { \
	*(dst)++ = *(src)++; \
}

#define COPY_DATA_2(dst, src) { \
	memcpy_s(dst, 2, src, 2); \
	dst += 2; \
	src += 2; \
}

#define COPY_DATA_3(dst, src) { \
	memcpy_s(dst, 3, src, 3); \
	dst += 3; \
	src += 3; \
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

// You can unmask off_events[][0] and max_velocity=0;
#define PUT_NOEVENT(dst, num) { \
	*(dst)++ = off_events[(num)].off[0] & 0x8F; \
	*(dst)++ = off_events[(num)].off[1]; \
	*(dst)++ = 0x7F; \
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
	printf("whole event length: %d\n", lEVNT);


	// pass 2 Simple decode
	unsigned char *midi_decode;

	if (NULL == (midi_decode = malloc(fsize * 4))) {
		fprintf(stderr, "Memory (decode buffer) allocation error\n");
		exit(errno);
	}
	unsigned char *dcur = midi_decode;

	int next_is_delta = 1;
	unsigned char *st = cur;
	unsigned oevents = 0;
	unsigned char *loopback = st;
	while (cur - st < lEVNT) {
		//		printf("%6d:", cur - st);

		if (*cur < 0x80) {
			unsigned delay = 0;
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
				PUT_DELTA(dcur, off_events[0].delta)
					PUT_NOEVENT(dcur, 0)
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

			PUT_DELTA(dcur, delay)
				next_is_delta = 0;
		}
		else {
			if (next_is_delta) {
				if (*cur >= 0x80) {
					*dcur++ = 0;
				}
			}

			next_is_delta = 1;
			if (*cur == 0xFF) {
				//				printf("META\n");
				if (*(cur + 1) == 0x2F) {
					if (oevents)
						printf("%3d note offs not closed\n", oevents);
					goto track_ends;
				}

				COPY_DATA_2(dcur, cur)
					unsigned textlen = *cur + 1;
				COPY_DATA(dcur, cur, textlen)
			}
			else if (0x90 == (*cur & 0xF0)) {
				COPY_DATA_3(dcur, cur)
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
			}
			else if ((0x80 == (*cur & 0xF0)) || (0xA0 == (*cur & 0xF0)) || (0xE0 == (*cur & 0xF0))) {
				COPY_DATA_3(dcur, cur)
			}
			else if ((0xC0 == (*cur & 0xF0)) || (0xD0 == (*cur & 0xF0))) {
				COPY_DATA_2(dcur, cur)
			}
			else if (0xB0 == (*cur & 0xF0)) {
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
//					printf("XMI special control %d\n", *(cur+1));
					*dcur++ = 0xFF;
					*dcur++ = 0x01;
					*dcur++ = 0x00;
					cur += 3;
					continue;
				default:
					COPY_DATA_3(dcur, cur)
				}
			}
			else {
				printf("wrong event\n");
				exit(-1);
			}
		}
	}

track_ends:;
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

		PUT_DELTA(tcur, delta)
			// last -  event
			if ((0x80 == (*pos & 0xF0)) || (0x90 == (*pos & 0xF0)) || (0xA0 == (*pos & 0xF0)) || (0xB0 == (*pos & 0xF0)) || (0xE0 == (*pos & 0xF0))) {
				COPY_DATA_3(tcur, pos)
			}
			else if ((0xC0 == (*pos & 0xF0)) || (0xD0 == (*pos & 0xF0))) {
				COPY_DATA_2(tcur, pos)
			}
			else if (0xF0 == *pos) {
				unsigned exlen = 0;
				COPY_DATA_1(tcur, pos)
					while (*pos & 0x80) {
						exlen += *pos & 0x7F;
						exlen <<= 7;
						COPY_DATA_1(cur, pos)
					}
				exlen += *pos & 0x7F;
				COPY_DATA(tcur, pos, 1 + exlen)
			}
			else if (0xF7 == *pos) {
				unsigned exlen = 0;
				COPY_DATA_1(tcur, pos)
					while (*pos & 0x80) {
						exlen += *pos & 0x7F;
						exlen <<= 7;
						COPY_DATA_1(tcur, pos)
					}
				exlen += *pos & 0x7F;
				COPY_DATA(tcur, pos, 1 + exlen)
			}
			else if (0xFF == *pos) {
				if (0x51 == *(pos + 1)) {
					COPY_DATA_3(tcur, pos)

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
					COPY_DATA_3(tcur, pos)
				}
				else {
					COPY_DATA_2(tcur, pos)
						unsigned textlen = *pos;
					COPY_DATA(tcur, pos, 1 + textlen)
				}
			}
			else {
				printf("Bad event %02x at %04x\n", *pos, pos - midi_decode);
				exit(-1);
			}
	}
	unsigned tlen = tcur - midi_write;


	//  pass 4 convert format 0 to 1
#define F1_TRACKS 17

	unsigned char *midi_write_f1[F1_TRACKS];
	unsigned char *f1pos[F1_TRACKS];
	unsigned f1delta[F1_TRACKS];

	for (unsigned i = 0; i < F1_TRACKS; i++) {
		if (NULL == (midi_write_f1[i] = malloc(fsize * 4))) {
			fprintf(stderr, "Memory (write buffer) allocation error\n");
			exit(errno);
		}
		f1pos[i] = midi_write_f1[i];
		f1delta[i] = 0;
	}

	unsigned char *f0pos = midi_write;

	while (f0pos < tcur) {
		// first delta-time
		unsigned delta = 0;
		while (*f0pos & 0x80) {
			delta += *f0pos++ & 0x7F;
			delta <<= 7;
		}
		delta += *f0pos++ & 0x7F;

		for (unsigned i = 0; i < F1_TRACKS; i++) {
			f1delta[i] += delta;
		}

		// last -  event
		if ((0x80 == (*f0pos & 0xF0)) || (0x90 == (*f0pos & 0xF0)) || (0xA0 == (*f0pos & 0xF0)) || (0xB0 == (*f0pos & 0xF0)) || (0xE0 == (*f0pos & 0xF0))) {
			unsigned track = (*f0pos & 0xF) + 1;
			PUT_DELTA(f1pos[track], f1delta[track])
				COPY_DATA_3(f1pos[track], f0pos)
				f1delta[track] = 0;
		}
		else if ((0xC0 == (*f0pos & 0xF0)) || (0xD0 == (*f0pos & 0xF0))) {
			unsigned track = (*f0pos & 0xF) + 1;
			PUT_DELTA(f1pos[track], f1delta[track])
				COPY_DATA_2(f1pos[track], f0pos)
				f1delta[track] = 0;
		}
		else if (0xF0 == *f0pos) {
			unsigned exlen = 0;
			PUT_DELTA(f1pos[0], f1delta[0])
				COPY_DATA_1(f1pos[0], f0pos)
				while (*f0pos & 0x80) {
					exlen += *f0pos & 0x7F;
					exlen <<= 7;
					COPY_DATA_1(f1pos[0], f0pos)
				}
			exlen += *pos & 0x7F;
			COPY_DATA(f1pos[0], f0pos, 1 + exlen)
				f1delta[0] = 0;
		}
		else if (0xF7 == *f0pos) {
			unsigned exlen = 0;
			PUT_DELTA(f1pos[0], f1delta[0])
				COPY_DATA_1(f1pos[0], f0pos)
				while (*f0pos & 0x80) {
					exlen += *f0pos & 0x7F;
					exlen <<= 7;
					COPY_DATA_1(f1pos[0], f0pos)
				}
			exlen += *f0pos & 0x7F;
			COPY_DATA(f1pos[0], f0pos, 1 + exlen)
				f1delta[0] = 0;
		}
		else if (0xFF == *f0pos) {
			if (0x2F == *(f0pos + 1)) {
				for (unsigned i = 0; i < F1_TRACKS; i++) {
					if (f1pos[i] - midi_write_f1[i]) {
						unsigned char *trend = f0pos;
						PUT_DELTA(f1pos[i], f1delta[i])
							COPY_DATA_3(f1pos[i], trend)
					}
				}
				f0pos += 3;
			}
			else {
				PUT_DELTA(f1pos[0], f1delta[0])
					COPY_DATA_2(f1pos[0], f0pos)
					unsigned textlen = *f0pos;
				COPY_DATA(f1pos[0], f0pos, 1 + textlen)
					f1delta[0] = 0;
			}
		}
		else {
			printf("Bad event %02x at %04x\n", *pos, pos - midi_decode);
			exit(-1);
		}
	}

	//	printf("%7d\n", tlen);
	unsigned char pt[_MAX_PATH], fn[_MAX_FNAME];

	_splitpath_s(argv[1], NULL, 0, NULL, 0, fn, _MAX_FNAME, NULL, 0);
	_makepath_s(pt, _MAX_PATH, NULL, NULL, fn, ".mid");

	// output
	if (fopen_s(&pFo, pt, "wb")) {
		fprintf(stderr, "File %s cannot open\n", pt);
		exit(errno);
	}

#ifdef FORMAT_0
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
#else
	// Form 1 write
	if (10 != fwrite(midiheaderf1, sizeof(unsigned char), 10, pFo)) {
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}
	unsigned short f1_tracks = F1_TRACKS;
	for (unsigned i = 0; i < F1_TRACKS; i++) {
		if ((f1pos[i] - midi_write_f1[i]) == 0) {
			f1_tracks--;
		}
	}
	unsigned short bs_f1_tracks = _byteswap_ushort(f1_tracks);
	if (1 != fwrite(&bs_f1_tracks, sizeof(unsigned short), 1, pFo)) {
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

	for (unsigned i = 0; i < F1_TRACKS; i++) {
		unsigned trlen = f1pos[i] - midi_write_f1[i];
		if (trlen) {
			if (4 != fwrite(midiTrheader, sizeof(unsigned char), 4, pFo)) {
				fprintf(stderr, "File write error\n");
				fclose(pFo);
				exit(errno);
			}

			unsigned bs_trlen = _byteswap_ulong(trlen);
			if (1 != fwrite(&bs_trlen, sizeof(unsigned), 1, pFo)) {
				fprintf(stderr, "File write error\n");
				fclose(pFo);
				exit(errno);
			}
			if (trlen != fwrite(midi_write_f1[i], sizeof(unsigned char), trlen, pFo)) {
				fprintf(stderr, "File write error\n");
				fclose(pFo);
				exit(errno);
			}
		}
	}

#endif
	fclose(pFo);
}