

#ifndef KALC_UI_H
#define KALC_UI_H


typedef struct BatchResult_t *      BatchResult;
typedef struct RowTag_t *           RowTag;
typedef struct RowTag_t             RowTag_t;

struct RowTag_t
{
	VariantBuf res;
	STRPTR     var;
};

struct BatchResult_t
{
	ListNode node;
	uint32_t alloc;
	RowTag_t results[32];
};

enum /* operating mode */
{
	MODE_EXPR,
	MODE_GRAPH,
	MODE_PROG
};

enum
{
	ACTION_BROWSE_PREV,
	ACTION_BROWSE_NEXT,
	ACTION_BROWSE_PPAGE,
	ACTION_BROWSE_NPAGE,
	ACTION_CLEAROREXIT,
	ACTION_ACCEPT,
	ACTION_DELETE,
	ACTION_DELALL
};

#define APPNAME       "PocketKalc"
#define VERSION       "1.1"

#ifdef __GNUC__
 #define COMPILER     "gcc " TOSTRING(__GNUC__) "." TOSTRING(__GNUC_MINOR__) "." TOSTRING(__GNUC_PATCHLEVEL__)
 #ifdef WIN32
  #if __x86_64__
   #define PLATFORM   "MS-Windows-x64"
  #else
   #define PLATFORM   "MS-Windows-x86"
  #endif
 #elif LINUX
  #if __x86_64__
   #define PLATFORM   "GNU-Linux-x64"
  #else
   #define PLATFORM   "GNU-Linux-x86"
  #endif
 #else
  #if __x86_64__
   #define PLATFORM   "Unknown-x64"
  #else
   #define PLATFORM   "Unknown-x86"
  #endif
 #endif
#else
 #define COMPILER    "unknown compiler"
 #ifdef WIN32
  #define PLATFORM   "MS-Windows-x86"
 #else
  #define PLATFORM   "GNU-Linux-x86"
 #endif
#endif

#endif
