/*
 * config.h: public functions to manage config file.
 *
 * written by T.Pierron, june 2022
 */

#ifndef KALC_CONFIG_H
#define KALC_CONFIG_H

void  configRead(STRPTR path);
void  configSave(void);
DATA8 configGetChunk(STRPTR name, int * size);
DATA8 configAddChunk(STRPTR name, int size);
void  configDelChunk(STRPTR name);

extern struct ConfigApp_t appcfg;
extern struct Config_t *  config;

typedef struct ConfigChunk_t *       ConfigChunk;
typedef struct ConfigApp_t *         ConfigApp;


struct Config_t
{
	ListHead chunks;
	DATA8    content;
	int      size, changed;
	uint8_t  oldConfig[64];
	TEXT     path[1];
};

struct ConfigChunk_t
{
	ListNode node;
	TEXT     name[16];
	DATA8    content;
	uint16_t size, oldSize;
	uint16_t changed;
};

struct ConfigApp_t
{
	uint16_t width, height;
	int      format, use64b;
	int      mode, lightMode;
	uint8_t  defUnitNames[32];
	int      defUnits[4];
};

#define SZ_CHUNK         128

#endif
