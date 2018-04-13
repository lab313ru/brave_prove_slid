#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int tag;
	int dest_size;
	int src_size;
	short wnd_size;
	short unkn;
} header_t, *header_p;

#define MAX_REPS (18)

#define WND_SIZE (0x400)
#define WND_MASK (WND_SIZE - 1)

#define SLID_TAG (0x534C4944)

unsigned char read_byte(const char *input, int *readoff)
{
	return (input[(*readoff)++]);
}

void write_byte(char *output, int *writeoff, unsigned char b)
{
	output[(*writeoff)++] = b;
}

unsigned short read_word(const char *input, int *readoff)
{
	short retn = read_byte(input, readoff);
	retn |= read_byte(input, readoff) << 8;
	return retn;
}

void write_word(char *output, int *writeoff, unsigned short w)
{
	write_byte(output, writeoff, w & 0xFF);
	write_byte(output, writeoff, w >> 8);
}

int read_cmd_bit(const char *input, int *readoff, char *bits, char *cmd)
{
	(*bits)--;

	if (!*bits)
	{
		*cmd = read_byte(input, readoff);
		*bits = 8;
	}

	int retn = *cmd & 1;
	*cmd >>= 1;
	return retn;
}

void write_cmd_bit(int bit, char *output, int *writeoff, char *bits, int *cmdoff)
{
	if (*bits == 8)
	{
		*bits = 0;
		*cmdoff = (*writeoff)++;
		output[*cmdoff] = 0;
	}

	output[*cmdoff] = ((bit & 1) << *bits) | output[*cmdoff];
	bit >>= 1;
	(*bits)++;
}

unsigned char read_wnd_byte(const char *window, int *wndoff)
{
	char b = window[*wndoff];
	*wndoff = (*wndoff + 1) & WND_MASK;
	return b;
}

void write_to_wnd(char *window, int *wndoff, unsigned char b)
{
	window[*wndoff] = b;
	*wndoff = (*wndoff + 1) & WND_MASK;
}

void init_wnd(char **wnd, char **reserve, int *wnd_off)
{
	*wnd = (char *)malloc(WND_SIZE);
	*reserve = (char *)malloc(WND_SIZE);
	(*wnd)[0] = 0;
	(*wnd)[1] = 0;

	*wnd_off = WND_SIZE - 0x12;

	for (int i = 2; i < *wnd_off; ++i) {
		(*wnd)[i] = 0;
	}
}

void find_matches(const char *input, int readoff, int size, int wndoff, char *window, char *reserve, int *reps, int *from)
{
	int wpos = 0, tlen = 0;

	*reps = 1;
	memcpy(reserve, window, WND_SIZE);

	while (wpos < WND_SIZE)
	{
		tlen = 0;
		while ((readoff + tlen < size && tlen < MAX_REPS) &&
			window[(wpos + tlen) & WND_MASK] == input[readoff + tlen])
		{
			window[(wndoff + tlen) & WND_MASK] = input[readoff + tlen];
			tlen++;
		}

		if (tlen > *reps)
		{
			*reps = tlen;
			*from = wpos & WND_MASK;
		}

		memcpy(window, reserve, WND_SIZE);
		wpos++;
	}
}

int slid_pack(const char *src, int src_size, char *dest, short some_size) {
	header_p header = (header_p)malloc(sizeof(header_t));

	int src_off = 0;
	int cmd_off = sizeof(header_t);
	int dst_off = cmd_off;

	header->tag = SLID_TAG;
	header->dest_size = src_size;
	header->wnd_size = WND_SIZE;
	header->unkn = some_size;

	char *wnd, *reserve;
	int wnd_off;

	init_wnd(&wnd, &reserve, &wnd_off);

	char cmd = 0;
	char bits = 8;

	int reps;
	int from;
	char b;

	while (src_off < src_size) {
		find_matches(src, src_off, src_size, wnd_off, wnd, reserve, &reps, &from);

		if (reps >= 1 && reps <= 2)
		{
			write_cmd_bit(1, dest, &dst_off, &bits, &cmd_off);
			b = read_byte(src, &src_off);
			write_byte(dest, &dst_off, b);
			write_to_wnd(wnd, &wnd_off, b);
		}
		else
		{
			write_cmd_bit(0, dest, &dst_off, &bits, &cmd_off);
			write_word(dest, &dst_off, ((from & 0x0F00) << 4) | (from & 0xFF) | (((reps - 3) & 0x0F) << 8));
			src_off += reps;

			for (int i = 0; i < reps; ++i)
			{
				b = read_wnd_byte(wnd, &from);
				write_to_wnd(wnd, &wnd_off, b);
			}
		}
	}

	free(wnd);
	free(reserve);

	header->src_size = dst_off;

	memcpy(dest, header, sizeof(header_t));

	return dst_off;
}

int slid_unpack(const char *src, char *dest, header_p header) {

	int dest_off = 0;
	int src_off = 0;

	int src_size = header->src_size - sizeof(header_t);
	int wnd_size = header->wnd_size;
	int wnd_mask = wnd_size - 1;
	int wnd_off = wnd_size - 0x12;

	if (wnd_off < 1) {
		return 0;
	}

	char *wnd, *reserve;

	init_wnd(&wnd, &reserve, &wnd_off);

	char cmd = 0;
	char bits = 1;
	char b;

	while (src_off < src_size) {
		int bit = read_cmd_bit(src, &src_off, &bits, &cmd);

		if (bit == 0) {
			short token = read_word(src, &src_off);
			int copy_off = ((token & 0xF000) >> 4) | (token & 0xFF); // (short)(((src[src_off + 1] & 0xF0) << 4) | (unsigned char)src[src_off + 0]);
			int copy_count = ((token & 0x0F00) >> 8) + 3; //(src[src_off + 1] & 0x0F) + 3;

			for (int i = 0; i < copy_count; ++i) {
				b = read_wnd_byte(wnd, &copy_off);
				write_to_wnd(wnd, &wnd_off, b);
				write_byte(dest, &dest_off, b);
			}
		} else {
			b = read_byte(src, &src_off);
			write_to_wnd(wnd, &wnd_off, b);
			write_byte(dest, &dest_off, b);
		}
	}

	free(reserve);
	free(wnd);

	return 1;
}

int main(int argc, char *argv[]) {
	if (argc < 4 && ((argv[3][0] != 'u') || (argv[3][0] != 'p'))) {
		printf("Usage: <src_file.bin> <dst_file.bin> <mode> [hex_offset|some_size]\n");
		printf("Unpack: src_file.bin dst_file.bin u 0\n");
		printf("Pack: src_file.bin dst_file.bin p");
		return 1;
	}

	FILE *f = fopen(argv[1], "rb");
	char mode = argv[3][0];

	long offset = 0;
	short some_size = 0x48;

	if (argc == 5 && mode == 'u') {
		sscanf(argv[4], "%zx", &offset);
	} else if (argc == 5 && mode == 'p') {
		sscanf(argv[4], "%hx", &some_size);
	}

	if (mode == 'u') {
		header_p header = (header_p)malloc(sizeof(header_t));
		fseek(f, offset, SEEK_SET);
		fread(header, 1, sizeof(header_t), f);

		if (header->tag != SLID_TAG) {
			printf("Not a SLID file!");
			return 1;
		}

		char *src = (char *)malloc(header->src_size - sizeof(header_t));
		fread(src, 1, header->src_size - sizeof(header_t), f);

		char *dest = (char *)malloc(header->dest_size);

		if (slid_unpack(src, dest, header) == 1) {
			FILE *d = fopen(argv[2], "wb");
			fwrite(dest, 1, header->dest_size, d);
			fclose(d);
		}

		free(header);
		free(src);
		free(dest);
	} else if (mode == 'p') {
		fseek(f, 0, SEEK_END);
		int src_size = (int)ftell(f);
		fseek(f, 0, SEEK_SET);

		char *src = (char *)malloc(src_size);
		fread(src, 1, src_size, f);

		char *dest = (char *)malloc(src_size + sizeof(header_t));

		int dest_size = slid_pack(src, src_size, dest, some_size);
		FILE *d = fopen(argv[2], "wb");
		fwrite(dest, 1, dest_size, d);
		fclose(d);

		free(src);
		free(dest);
	}

	fclose(f);
}