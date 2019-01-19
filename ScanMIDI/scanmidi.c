#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>

#define PUT_DELTA(ptr, _delta) { \
	unsigned _tdelta = (_delta); \
	unsigned _tdelay = _tdelta & 0x7F; \
	while ((_tdelta >>= 7)) { \
		_tdelay <<= 8; \
		_tdelay |= ((_tdelta & 0x7F) | 0x80); \
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

#define SETUP_MEAS 4

int main(int argc, char **argv)
{
	FILE *pFi, *pFo;

	if (argc != 3) {
		fprintf(stderr, "Usage:%s infile outfile\n", argv[0]);
		exit(-1);
	}

	if (fopen_s(&pFi, argv[1],"rb")){
		fprintf(stderr, "File %s cannot open\n", argv[1]);
		exit(errno);
	}

	fseek(pFi, 0, SEEK_END);
	signed char *midi_data, *out_data;
	long fsize = ftell(pFi);
	fseek(pFi, 0, SEEK_SET);

	printf("DEBUG: fsize %ld bytes\n", fsize);
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
	printf("DEBUG: file read complete goto on-memory\n");

	if (memcmp(midi_data, "MThd", 4)) {
		fprintf(stderr, "Not MIDI file\n");
	}
	unsigned hlen = _byteswap_ulong(*((unsigned *)&midi_data[4]));
	unsigned short format = _byteswap_ushort(*((unsigned short *)&midi_data[8]));
	unsigned short tracks = _byteswap_ushort(*((unsigned short *)&midi_data[10]));
	unsigned short timebase = _byteswap_ushort(*((unsigned short *)&midi_data[12]));
	unsigned short setup_meas_len = timebase * 4 * SETUP_MEAS;
	printf("header length: %ld Format:%d %d tracks Timebase:%d\n", hlen, format, tracks, timebase);

	if (NULL == (out_data = malloc(fsize * 2))) {
		fprintf(stderr, "Memory allocation error\n");
		fclose(pFi);
		exit(errno);
	}

	memcpy_s(out_data, fsize, midi_data, 8 + hlen);
	unsigned toffset = 8 + hlen;
	unsigned out_pos = toffset;
	for (int i = 0; i < tracks; i++) {
		printf("track %d\n", i);
		if (memcmp(&midi_data[toffset], "MTrk", 4)) {
			fprintf(stderr, "Not MIDI track\n");
		}
		memcpy_s(&out_data[out_pos], fsize, &midi_data[toffset], 4);

		unsigned tlen = _byteswap_ulong(*((unsigned *)&midi_data[toffset + 4]));
		printf("track length = %d\n", tlen);
		signed char *pos = &midi_data[toffset + 8];
		signed char *dpos = &out_data[out_pos + 8];
		signed char *dpos_keep = dpos;
		signed char prev, cur = 0;

		unsigned kdelta = 0 + setup_meas_len; // force append blank setup measure.
		while (pos < &midi_data[toffset + 8 + tlen]) {
			// first delta-time
			signed char *kdpos;
			unsigned delta = 0;
			while (*pos < 0) {
				delta += *pos & 0x7F;
				delta <<= 7;
				pos++;
			}
			delta += *pos & 0x7F;
			pos++;

			if (kdelta) {
				delta += kdelta;
//				printf("total delta %10lu\n", delta);
			}
			kdelta = 0;
			kdpos = dpos;
//			printf("D %5ld/", delta);
//			printf("dposa = %08lX, %01lX, next=%01lX\n", dpos, *(dpos-1), *pos);
			PUT_DELTA(dpos, delta)

//			printf("dposb = %08lx\n", dpos);

			prev = cur;
			cur = *pos;

			// last event
			if (0x00 == (*pos & 0x80)) {
//				printf("Running Status\n");
//				printf("%01x %01x\n", prev, cur);
				cur = prev;
				*dpos++ = prev; // remove running status
				if ((0xC0 == (prev & 0xF0)) || (0xD0 == (prev & 0xF0))) {
					*dpos++ = *pos++;
				}
				else {
					*dpos++ = *pos++;
					*dpos++ = *pos++;
				}
			}
			else if (0x80 == (*pos & 0xF0)) {
//				printf("Note off\n");
				*dpos++ = *pos++;
				*dpos++ = *pos++;
				*dpos++ = *pos++;
			}
			else if (0x90 == (*pos & 0xF0)) {
//				printf("Note on\n");
				*dpos++ = *pos++;
				*dpos++ = *pos++;
				*dpos++ = *pos++;
			}
			else if (0xA0 == (*pos & 0xF0)) {
//				printf("Key pressure\n");
				*dpos++ = *pos++;
				*dpos++ = *pos++;
				*dpos++ = *pos++;
			}
			else if (0xB0 == (*pos & 0xF0)) {
//				printf("control change\n");
				*dpos++ = *pos++;
				*dpos++ = *pos++;
				*dpos++ = *pos++;
			}
			else if (0xC0 == (*pos & 0xF0)) {
//				printf("program change\n");
				*dpos++ = *pos++;
				*dpos++ = *pos++;
			}
			else if (0xD0 == (*pos & 0xF0)) {
//				printf("channel pressure\n");
				*dpos++ = *pos++;
				*dpos++ = *pos++;
			}
			else if (0xE0 == (*pos & 0xF0)) {
//				printf("pitch bend\n");
				*dpos++ = *pos++;
				*dpos++ = *pos++;
				*dpos++ = *pos++;
			}
			else if (0xF0 == (*pos & 0xFF)) {
				unsigned exlen = 0;
				*dpos++ = *pos++;
				while (*pos < 0) {
					exlen += *pos & 0x7F;
					exlen <<= 7;
					*dpos++ = *pos++;
				}
				exlen += *pos & 0x7F;
//				printf("F0 Exlen %ld\n", exlen);
				while (exlen--) {
					*dpos++ = *pos++;
				}
				*dpos++ = *pos++;

			}
			else if (0xF7 == (*pos & 0xFF)) {
				unsigned exlen = 0;
				*dpos++ = *pos++;
				while (*pos < 0) {
					exlen += *pos & 0x7F;
					exlen <<= 7;
					*dpos++ = *pos++;
				}
				exlen += *pos & 0x7F;
//				printf("F7 Exlen %ld\n", exlen);
				while (exlen--) {
					*dpos++ = *pos++;
				}
				*dpos++ = *pos++;
			}
			else if (0xFF == (*pos & 0xFF)) {
				*dpos++ = *pos++;
				*dpos++ = *pos++;
//				printf("META\n");
				unsigned textlen = *pos;
				while (textlen--) {
					*dpos++ = *pos++;
				}
				*dpos++ = *pos++;
			}
			else if (0xFE == (*pos & 0xFF)) {
				unsigned ut = *(pos+1);
				unsigned ulen = *(pos+2);
//				printf("D %5ld/Unknown %02x %02x\n", delta, ut, ulen);
				cur = prev;
				*pos = 0xFF;
				*(pos + 1) = 0x7F;
				pos += ulen + 3;
				dpos = kdpos;
				kdelta = delta;
			}
			else {
				printf("Bad event %02x\n", *pos);
			}

		}
		toffset += 8 + tlen;

		unsigned int ntlen = dpos - dpos_keep;
		printf("New track length=%d\n", ntlen);
		*((unsigned *)&out_data[out_pos + 4]) = _byteswap_ulong(ntlen);
		out_pos += 8 + ntlen;
	}
	if (fopen_s(&pFo, argv[2], "wb")){
		fprintf(stderr, "File %s cannot open\n", argv[2]);
		exit(errno);
	}
	printf("size %08lu/%08ld/%08ld\n", fsize, toffset, out_pos);

#if 0
	if (fsize != fwrite(midi_data, sizeof(signed char), fsize, pFo)) {
#else
	if (out_pos != fwrite(out_data, sizeof(signed char), out_pos, pFo)) {
#endif
		fprintf(stderr, "File write error\n");
		fclose(pFo);
		exit(errno);
	}
	fclose(pFo);

}