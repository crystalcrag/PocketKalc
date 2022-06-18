/*
 * config.c : manage config file: this used to be written as an INI file, but this file contains binary
 *            data and multi-line strings: INI is not the best tool for this job.
 *
 * written by T.Pierron, june 2022
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "UtilityLibLite.h"
#include "config.h"


struct Config_t *  config;
struct ConfigApp_t appcfg;

static uint8_t configHeader[] = "CONFIG v1.0____\n";

void configRead(STRPTR path)
{
	FILE * in = fopen(path, "rb");
	int length = strlen(path);

	if (in)
	{
		int size;
		fseek(in, 0, SEEK_END); size = ftell(in);
		fseek(in, 0, SEEK_SET);

		config = malloc(sizeof *config + size + length);
		memset(config, 0, sizeof *config);
		config->content = (DATA8) (config+1) + length;
		config->size = size;
		ListNew(&config->chunks);
		strcpy(config->path, path);

		fread(config->content, 1, size, in);
		fclose(in);

		/* 16 bytes header */
		if (strncmp(configHeader, config->content, 16) == 0)
		{
			DATA8 cfg, eof;

			/* get list of chunks in this config */
			for (cfg = config->content + 16, eof = cfg + size - 16; cfg < eof; cfg += size + 16)
			{
				/* chunk hdr: 2bytes size (BE) + 14 bytes name */
				size = (cfg[0] << 8) | cfg[1];
				if (cfg + size > eof) return;

				ConfigChunk chunk = calloc(sizeof *chunk, 1);

				memcpy(chunk->name, cfg + 2, 14);
				chunk->name[15] = 0;
				chunk->size = chunk->oldSize = size;
				chunk->content = cfg + 16;

				ListAddTail(&config->chunks, &chunk->node);
			}
		}
	}
	else /* create a default config */
	{
		config = calloc(sizeof *config + length, 1);
		config->changed = 1;
		strcpy(config->path, path);
	}

	/* already read config chunk */
	DATA8 mem = configGetChunk("_CONFIG", NULL);

	if (mem)
	{
		appcfg.width  = (mem[0] << 8) | mem[1];
		appcfg.height = (mem[2] << 8) | mem[3];
		appcfg.format = mem[4];
		appcfg.mode   = mem[5];
		appcfg.use64b = mem[6];
		appcfg.lightMode = mem[7];
		memcpy(appcfg.defUnitNames, mem + 16, sizeof appcfg.defUnitNames);
	}
	else /* set default values */
	{
		ConfigChunk chunk = calloc(sizeof *chunk, 1);
		strcpy(chunk->name, "_CONFIG");
		chunk->content = mem = calloc(SZ_CHUNK, 1);
		chunk->size = SZ_CHUNK;
		ListAddTail(&config->chunks, &chunk->node);

		appcfg.width  = 640;
		appcfg.height = 480;
		appcfg.use64b = 1;
		appcfg.lightMode = 1;
		strcpy(appcfg.defUnitNames, "M/degC/G");
	}
	memcpy(config->oldConfig, mem, sizeof config->oldConfig);
}

/* will overwrite file read in configRead() */
void configSave(void)
{
	ConfigChunk chunk;
	FILE *      out;
	Bool        modif = config->changed;
	/* note: this is done at program exit */
	out = fopen(config->path, modif ? "wb" : "rb+");
	if (! out) return;

	if (modif)
		fwrite(configHeader, 1, 16, out);
	else
		fseek(out, 16, SEEK_SET);

	/* don't care if it is modified or not */
	for (chunk = HEAD(config->chunks); chunk; NEXT(chunk))
	{
		uint8_t header[16];
		header[0] = chunk->size >> 8;
		header[1] = chunk->size & 0xff;
		memcpy(header + 2, chunk->name, 14);

		if (strcasecmp(chunk->name, "_CONFIG") == 0)
		{
			/* store appcfg back into chunk mem */
			DATA8 mem = chunk->content;
			mem[0] = appcfg.width >> 8;
			mem[1] = appcfg.width & 0xff;
			mem[2] = appcfg.height >> 8;
			mem[3] = appcfg.height & 0xff;
			mem[4] = appcfg.format;
			mem[5] = appcfg.mode;
			mem[6] = appcfg.use64b;
			mem[7] = appcfg.lightMode;
			memcpy(mem + 16, appcfg.defUnitNames, sizeof appcfg.defUnitNames);
			if (memcmp(mem, config->oldConfig, sizeof config->oldConfig))
				chunk->changed = 1;
		}

		/* do not overwrite stuff that hasn't changed */
		if (! modif && ! chunk->changed && chunk->oldSize == chunk->size)
		{
			//fprintf(stderr, "skipping %s\n", chunk->name);
			fseek(out, chunk->size + 16, SEEK_CUR);
			continue;
		}
		if (chunk->oldSize != chunk->size)
			/* overwrite remaining */
			modif = True;

		//fprintf(stderr, "writing %s\n", chunk->name);
		fwrite(header, 1, sizeof header, out);
		fwrite(chunk->content, 1, chunk->size, out);
		chunk->oldSize = chunk->size;
	}
	fclose(out);
}

DATA8 configGetChunk(STRPTR name, int * size)
{
	ConfigChunk chunk;

	for (chunk = HEAD(config->chunks); chunk; NEXT(chunk))
	{
		if (strcasecmp(chunk->name, name) == 0)
		{
			if (size) *size = chunk->size;
			return chunk->content;
		}
	}
	if (size) *size = 0;
	return NULL;
}

DATA8 configAddChunk(STRPTR name, int size)
{
	ConfigChunk chunk;
	/* round up to next chunk multiple */
	size = (size + SZ_CHUNK - 1) & ~(SZ_CHUNK-1);
	for (chunk = HEAD(config->chunks); chunk; NEXT(chunk))
	{
		if (strcasecmp(chunk->name, name) == 0)
		{
			DATA8 mem = chunk->content;
			if (size > chunk->size)
			{
				/* enlarge buffer */
				if (config->content <= mem && mem < config->content + config->size)
					mem = NULL;

				chunk->content = mem = realloc(mem, size);
				/* might contain some sensitive information */
				memset(mem + chunk->oldSize, 0, size - chunk->oldSize);
				chunk->size = size;
			}
			else if (size < chunk->size)
			{
				/* prevent old data from leaking */
				memset(chunk->content + size, 0, chunk->size - size);
			}
			chunk->changed = 1;
			return mem;
		}
	}

	/* chunk not in list yet, add it now */
	chunk = calloc(sizeof *chunk, 1);
	CopyString(chunk->name, name, 15);
	ListAddTail(&config->chunks, &chunk->node);

	chunk->size = size;
	chunk->changed = 1;
	chunk->content = calloc(size, 1);
	return chunk->content;
}

void configDelChunk(STRPTR name)
{
	ConfigChunk chunk;
	for (chunk = HEAD(config->chunks); chunk; NEXT(chunk))
	{
		if (strcasecmp(chunk->name, name) == 0)
		{
			DATA8 mem = config->content;
			ListRemove(&config->chunks, &chunk->node);
			if (! (config->content <= mem && mem < config->content + config->size))
				free(mem);
			free(chunk);
			/* rewrite everything */
			config->changed = 1;
			break;
		}
	}
}
