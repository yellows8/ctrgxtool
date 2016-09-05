#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

unsigned int total_vertices = 0;
unsigned int vertlist_type = 0;

typedef struct {
	unsigned char ID_size;
	unsigned char palettetype;//0=none, 1=palette used
	unsigned char colortype;//2=rgb, etc

	unsigned short palette_firstent;
	unsigned short palette_totalcolors;
	unsigned char palette_colorbitsize;

	unsigned short xorigin;
	unsigned short yorigin;
	unsigned short width;
	unsigned short height;
	unsigned char bpp;
	unsigned char descriptor;//bit4 set=horizontal flip, bit5 set=vertical flip
} __attribute((packed)) tga_header;

void print_parambuf(unsigned int cmdhdr, unsigned int *parambuf, FILE *fout)
{
	unsigned int pos, entindex;
	unsigned int cmdid;
	unsigned int parambuf_wordsz;
	float *paramf;

	parambuf_wordsz = ((cmdhdr<<1)>>21) + 1;
	cmdid = cmdhdr & 0xFFFFF;
	paramf = (float*)parambuf;

	pos = 0;
	entindex = 0;
	while(pos<parambuf_wordsz)
	{
		switch(cmdid)
		{
			case 0xF02C1:
				if(parambuf_wordsz - pos < 3)return;
				printf("Entry 0x%lx: 0 = float %f u32 %08lx, 1 = float %f u32 %08lx, 2 = float %f u32 %08lx, 3 = float %f u32 %08lx\n", entindex, paramf[pos], parambuf[pos], paramf[pos+1], parambuf[pos+1], paramf[pos+2], parambuf[pos+2], paramf[pos+3], parambuf[pos+3]);

				pos+=4;
				entindex++;
			break;

			default:
				printf("Param 0x%lx: float %f u32 %08lx\n", pos, paramf[pos], parambuf[pos]);
				pos++;
			break;
		}
	}
}

void parse_gpucommands(unsigned int *buf, unsigned int bufsz, FILE *fout)
{
	unsigned int pos;

	unsigned int cmdhdr;
	float *paramf;
	unsigned int *param;
	unsigned int *parambuf;
	unsigned int parambuf_size;
	unsigned int parambuf_pos;

	pos = 0;
	while(pos < bufsz>>2)
	{
		paramf = (float*)&buf[pos];
		param = (unsigned int*)&buf[pos];
		cmdhdr = buf[pos+1];
		parambuf_size = (((cmdhdr<<1)>>21) + 1) * 4;
		printf("%08x: CmdHdr %08lx", pos<<2, cmdhdr);

		if((cmdhdr & 0xFFFFF) == 0xF02C0)vertlist_type = *param;

		if(parambuf_size == 4)
		{
			printf(", float param %f, u32 param 0x%08x\n", *paramf, *param);
			pos+=2;
		}
		else
		{
			printf("\n");
			pos+=2;

			parambuf = (unsigned long*)malloc(parambuf_size);
			memset(parambuf, 0, parambuf_size);
			printf("Command parameter buffer size: 0x%x\n", parambuf_size);
			
			parambuf[0] = *param;
			for(parambuf_pos=1; parambuf_pos < (parambuf_size>>2); parambuf_pos++)
			{
				parambuf[parambuf_pos] = buf[pos];
				pos++;
			}
			if(pos & 1)pos++;

			print_parambuf(cmdhdr, parambuf, fout);
			
			free(parambuf);
		}
	}
}

void parse_sharedmem(unsigned int *buf, unsigned int bufsz)
{
	unsigned int threadindex, cmdindex;
	unsigned int cmdid;
	unsigned int *cmdbuf, *cmd;

	for(threadindex=0; threadindex<4; threadindex++)
	{
		cmdbuf = &buf[(0x800 + threadindex*0x200)>>2];
		if(cmdbuf[0]==0)break;

		printf("ThreadIndex %u:\n", threadindex);

		printf("Command BufHdr: 0x%08lx\n", cmdbuf[0]);
		printf("Current command index: 0x%lx\n\n", cmdbuf[0] & 0xff);

		for(cmdindex=0; cmdindex<15; cmdindex++)
		{
			cmd = &cmdbuf[(0x20 + cmdindex*0x20)>>2];
			if(cmd[0]==0)continue;
			printf("CmdIndex 0x%x: ", cmdindex);

			cmdid = cmd[0] & 0xff;
			printf("CmdID 0x%lx ", cmdid);

			switch(cmdid)
			{
				case 0:
					printf("DMA, src addr 0x%lx, dst addr 0x%lx, size 0x%lx", cmd[1], cmd[2], cmd[3]);
				break;

				case 1:
					printf("GPU cmds, buf addr 0x%lx, buf size 0x%lx, flag 0x%lx, dcache flush flag 0x%lx", cmd[1], cmd[2], cmd[3], cmd[7]);
				break;

				case 2:
					printf("Buf0 addr 0x%lx, buf0 size 0x%lx, assoc buf0 addr 0x%lx, buf1 addr 0x%lx, buf1 size 0x%lx, assoc buf1 addr 0x%lx, buf0 u16 0x%lx, buf1 u16 0x%lx", cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7] & 0xffff, cmd[7]>>16);
				break;

				case 3:
					printf("Framebuf render, VRAM addr 0x%lx, out framebuf addr 0x%lx, VRAM framebuf dimensions 0x%lx, out framebuf dimensions 0x%lx, unk 0x%lx", cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);
				break;

				case 4:
					printf("Framebuf render4, buf0 addr 0x%lx, buf1 addr 0x%lx, unk3 0x%lx, unk4 0x%lx, unk5 0x%lx, unk6 0x%lx", cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6]);
				break;

				case 5:
					printf("DCache flush, buf0 addr 0x%lx, buf0 size 0x%lx, buf1 addr 0x%lx, buf1 size 0x%lx, buf2 addr 0x%lx, buf2 size 0x%lx", cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6]);
				break;
			}

			printf("\n");
		}
		printf("\n");
	}
}

void convert_texture(unsigned int *buf, unsigned int bufsz, FILE *fout)
{
	unsigned int x, y;
	unsigned int incolor = 2;
	unsigned int inpix;
	unsigned char *inpixdata = (unsigned char*)buf;
	unsigned char outpix[4];
	tga_header tgahdr;

	if(fout==NULL)
	{
		printf("Specify an output path.\n");
		return;
	}

	memset(&tgahdr, 0, sizeof(tga_header));
	memset(outpix, 0, 4);

	tgahdr.colortype = 2;
	tgahdr.width = 64;
	tgahdr.height = 64;
	tgahdr.bpp = 24;

	fwrite(&tgahdr, 1, sizeof(tga_header), fout);

	for(x=0; x<tgahdr.width; x++)
	{
		for(y=0; y<tgahdr.height; y++)
		{
			inpix = *((unsigned int*)&inpixdata[(y*tgahdr.width + x) * 2]);

			outpix[0] = (inpix & 0x1f) << 3;
			outpix[1] = ((inpix>>5) & 0x3f) << 2;
			outpix[2] = ((inpix>>11) & 0x1f) << 3;

			fwrite(outpix, 1, 3, fout);
		}
	}
}

int main(int argc, char **argv)
{
	FILE *f, *fout = NULL;
	int argi;
	unsigned int *buf;
	unsigned int filepos = 0;
	unsigned int bufsz = 0;
	unsigned int parsetype = 0;
	struct stat filestat;

	if(argc<2)return 0;

	if(stat(argv[1], &filestat)==-1)
	{
		printf("Failed to stat the input file: %s\n", argv[1]);
		return 0;
	}

	if(argc>=3)
	{
		for(argi=2; argi<argc; argi++)
		{
			if(strncmp(argv[argi], "--sharedmem", 11)==0)
			{
				parsetype = 1;
			}
			if(strncmp(argv[argi], "--convtex", 9)==0)
			{
				parsetype = 2;
			}
			if(strncmp(argv[argi], "--outpath=", 10)==0)
			{
				fout = fopen(&argv[argi][10], "wb");
			}
			if(strncmp(argv[argi], "--filepos=", 10)==0)
			{
				sscanf(&argv[argi][10], "0x%x", &filepos);
			}
			if(strncmp(argv[argi], "--size=", 7)==0)
			{
				sscanf(&argv[argi][7], "0x%x", &bufsz);
			}
		}
	}

	if(bufsz == 0)
	{
		bufsz = filestat.st_size;
		if(bufsz & 7)
		{
			printf("Input command file-size is not 8-byte aligned.\n");
			if(fout)fclose(fout);
			return 0;
		}
	}

	buf = (unsigned int*)malloc(bufsz);
	if(buf==NULL)
	{
		printf("Failed to allocate memory.\n");
		if(fout)fclose(fout);
		return 0;
	}

	f = fopen(argv[1], "rb");
	fseek(f, filepos, SEEK_SET);
	fread(buf, 1, bufsz, f);
	fclose(f);

	if(parsetype==0)parse_gpucommands(buf, bufsz, fout);
	if(parsetype==1)parse_sharedmem(buf, bufsz);
	if(parsetype==2)convert_texture(buf, bufsz, fout);

	free(buf);
	if(fout)fclose(fout);

	printf("Done.\n");

	return 0;
}

