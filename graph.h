/*
 * graph.h: public functions to draw a function over a graph.
 */

#ifndef KALC_GRAPH_H
#define KALC_GRAPH_H


void graphInit(SIT_Widget);
void graphSetFunc(STRPTR expr);
void graphSetPeek(int set, Bool vertical);
void graphReset(void);
STRPTR graphGetFunc(void);


struct Graph_t
{
	SIT_Widget canvas;
	float      range, step;
	float      gradX, gradY;
	float      dx, dy;
	float      grad, width, height;
	TEXT       function[256];
	float *    interpol;
	float      curveStartX;
	float      mouseX, mouseY;
	double     peekVal;
	TEXT       peekX[64];
	TEXT       peekY[20];
	uint8_t    peekLine;
	uint8_t    refresh;
	uint8_t    waitConf;
	uint8_t    hover;
	int        count;
};


#endif
