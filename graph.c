/*
 * graph.c: simple module to draw a function over a graph
 *
 * written by T.Pierron, may 2022.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "nanovg.h"
#include "SIT.h"
#include "parse.h"
#include "config.h"
#include "graph.h"


static struct Graph_t graph;

static int graphResize(SIT_Widget w, APTR cd, APTR ud)
{
	graph.refresh = 1;
	graph.waitConf = 1;
	return 1;
}

static void graphRefreshCache(float width)
{
	float onePx = graph.range / width;
	float start = - roundf(width * 0.5f + graph.dx) * onePx;
	int i, count = width / 2;

	if (graph.count < count)
	{
		graph.count = count;
		graph.interpol = realloc(graph.interpol, count * 4);
	}

	struct ParseExprData_t expr = {.res = {.type = TYPE_DBL}};

	graph.curveStartX = start;
	for (onePx *= 2, i = 0; i < count; i ++)
	{
		expr.res.type = TYPE_DBL;
		expr.res.real64  = start + i * onePx;
		if (ParseExpression(graph.function, parseExpr, &expr) == 0)
		{
			switch (expr.res.type) {
			case TYPE_INT32: graph.interpol[i] = expr.res.int32; break;
			case TYPE_INT:   graph.interpol[i] = expr.res.int64; break;
			case TYPE_DBL:   graph.interpol[i] = isnan(expr.res.real64) ? INFINITY : expr.res.real64; break;
			case TYPE_FLOAT: graph.interpol[i] = isnan(expr.res.real32) ? INFINITY : expr.res.real32; break;
			default:         graph.interpol[i] = INFINITY;
			}
		}
		else graph.interpol[i] = INFINITY;
	}
}

static int graphPaint(SIT_Widget w, APTR cd, APTR ud)
{
	SIT_OnPaint * paint = cd;
	NVGcontext *  vg = paint->nvg;
	if (graph.waitConf)
	{
		static float roundTo[] = {1, 1, 2, 5, 5, 5, 10, 10, 10, 10};
		float step = graph.range / floorf(paint->w / 75);
		float rounded;

		if (step < 1)
		{
			for (rounded = 10; step * rounded < 1; rounded *= 10);
			step = roundTo[(int) (step * rounded)] / rounded;
		}
		else if (step >= 10)
		{
			for (rounded = 10; step / rounded >= 10; rounded *= 10);
			step = roundTo[(int) (step / rounded)] * rounded;
		}
		else step = (int) step;

		graph.step = step;
		graph.grad = graph.range / graph.step;
		graph.waitConf = 0;
		graph.width = paint->w;
		graph.height = paint->h;
	}
	float cx = roundf(paint->x + paint->w * 0.5 + graph.dx);
	float cy = roundf(paint->y + paint->h * 0.5 + graph.dy);

	nvgStrokeWidth(vg, 2);
	nvgScissor(vg, paint->x, paint->y, paint->w, paint->h);

	nvgBeginPath(vg);
	nvgMoveTo(vg, paint->x, cy);
	nvgLineTo(vg, paint->x + paint->w, cy);

	nvgMoveTo(vg, cx, paint->y);
	nvgLineTo(vg, cx, paint->y + paint->h);

	/* graduation */
	float scale = paint->w / graph.grad;
	float max, pos;
	int   i, j;

	j = floorf(- cx / scale);
	pos = j * scale + cx;

	/* horizontal */
	for (max = paint->x + paint->w; pos < max; j ++)
	{
		float next = cx + roundf((j + 1) * scale);
		nvgMoveTo(vg, pos, cy);
		nvgLineTo(vg, pos, cy - 12);
		if (j != 0)
		{
			TEXT num[16];
			snprintf(num, sizeof num, "%g", j * graph.step);
			nvgText(vg, pos - nvgTextBounds(vg, 0, 0, num, NULL, NULL) * 0.5f, cy + 5, num, NULL);
		}
		/* sub graduation */
		for (i = 1; i < 10; i ++)
		{
			float grad = pos + roundf((next - pos) * i / 10);
			nvgMoveTo(vg, grad, cy);
			nvgLineTo(vg, grad, cy - 6);
		}
		pos = next;
	}

	/* vertical */
	j = ceilf((paint->y + paint->h - cy) / scale);
	pos = j * scale + cy;

	float fh = paint->fontSize * 0.5f;
	for (; pos >= paint->y; j --)
	{
		float next = cy + roundf((j - 1) * scale);
		nvgMoveTo(vg, cx,      pos);
		nvgLineTo(vg, cx + 12, pos);
		if (j != 0)
		{
			TEXT num[16];
			snprintf(num, sizeof num, "%g", - j * graph.step);
			nvgText(vg, cx - nvgTextBounds(vg, 0, 0, num, NULL, NULL) - 5, pos - fh, num, NULL);
		}

		/* sub graduation */
		for (i = 1; i < 10; i ++)
		{
			float grad = pos + roundf((next - pos) * i / 10);
			nvgMoveTo(vg, cx,     grad);
			nvgLineTo(vg, cx + 6, grad);
		}
		pos = next;
	}

	nvgStroke(vg);
	nvgStrokeWidth(vg, 1);

	if (graph.function[0])
	{
		nvgText(vg, paint->x + 5, paint->y + 5, graph.function, NULL);

		if (graph.refresh)
		{
			graphRefreshCache(paint->w);
			graph.refresh = 0;
		}

		nvgBeginPath(vg);


		float step = 2 * graph.range / paint->w, y;
		int   skip = 1;

		scale = paint->w / graph.range;

		for (i = 0; i < graph.count; i ++)
		{
			if (graph.interpol[i] == INFINITY)
			{
				skip = 1;
				continue;
			}
			pos = cx + roundf((graph.curveStartX + i * step) * scale);
			y   = cy - roundf(graph.interpol[i] * scale);
			if (skip)
				nvgMoveTo(vg, pos, y), skip = 0;
			else
				nvgLineTo(vg, pos, y);
		}

		nvgStroke(vg);
	}

	float len;
	if (! graph.hover) return 1;
	switch (graph.peekLine) {
	case 1:
		/* X first, then Y */
		len = nvgTextBounds(vg, 0, 0, graph.peekX, NULL, NULL);
		nvgText(vg, paint->x + paint->w - len - 5, paint->y + 5, graph.peekX, NULL);
		len = nvgTextBounds(vg, 0, 0, graph.peekY, NULL, NULL);
		nvgText(vg, paint->x + paint->w - len - 5, paint->y + paint->fontSize * 1.1 + 5, graph.peekY, NULL);

		pos = paint->x + round((graph.peekVal - graph.curveStartX) * paint->w / graph.range);
		nvgBeginPath(vg);
		nvgStrokeColorRGBA8(vg, "\0\0\0\x7f");
		nvgMoveTo(vg, pos, paint->y);
		nvgLineTo(vg, pos, paint->y + paint->h);
		nvgStroke(vg);
		break;
	case 2:
		/* Y first, then X */
		len = nvgTextBounds(vg, 0, 0, graph.peekY, NULL, NULL);
		nvgText(vg, paint->x + paint->w - len - 5, paint->y + 5, graph.peekY, NULL);
		len = nvgTextBounds(vg, 0, 0, graph.peekX, NULL, NULL);
		nvgText(vg, paint->x + paint->w - len - 5, paint->y + paint->fontSize * 1.1 + 5, graph.peekX, NULL);

		pos = round(cy - graph.peekVal * paint->w / graph.range);
		nvgBeginPath(vg);
		nvgStrokeColorRGBA8(vg, "\0\0\0\x7f");
		nvgMoveTo(vg, paint->x, pos);
		nvgLineTo(vg, paint->x + paint->w, pos);
		nvgStroke(vg);
	}

	return 1;
}

static void graphZoom(int dir)
{
	graph.range *= dir < 0 ? 0.5f : 2.0f;
	graph.refresh = 1;
	graph.waitConf = 1;
	SIT_ForceRefresh();
}

static Bool graphIntersect(int index, float y)
{
	if (index >= 0 && index < graph.count - 1)
	{
		float * val = graph.interpol + index;
		return val[0] < val[1] ? val[0] <= y && y <= val[1] : val[1] <= y && y <= val[0];
	}
	return False;
}

static void graphSetPeekStr(void)
{
	if (graph.function[0])
	{
		if (graph.peekLine == 1) /* value of function at X */
		{
			double x = graph.curveStartX + graph.mouseX * graph.range / graph.width;

			/* round x to nearest <step> value */
			x -= fmod(x, graph.step * 0.1);

			struct ParseExprData_t expr = {.res = {.type = TYPE_DBL}};
			expr.res.real64 = x;
			graph.peekVal = x;

			snprintf(graph.peekX, sizeof graph.peekX, "X = %g", x);
			strcpy(graph.peekY, "Y = NAN");
			if (ParseExpression(graph.function, parseExpr, &expr) == 0)
			{
				strcpy(graph.peekY, "Y = ");
				ToString(&expr.res, graph.peekY + 4, sizeof graph.peekY - 4);
			}
		}
		else /* value(s) of X that intersect horizontal line Y */
		{
			double y = (graph.height * 0.5 + graph.dy - graph.mouseY) * graph.range / graph.width;

			y -= fmod(y, graph.step * 0.1);

			graph.peekVal = y;
			graph.peekX[0] = 0;
			snprintf(graph.peekY, sizeof graph.peekY, "Y = %g", y);

			int pivot = graph.mouseX * graph.count / graph.width;
			int max = graph.count - pivot, i;
			if (max < pivot) max = pivot;
			for (i = 1; i < max; i ++)
			{
				float * val;
				if (graphIntersect(pivot - i, y))
					/* check before */
					val = graph.interpol + pivot - i;
				else if (graphIntersect(pivot + i - 1, y))
					/* check after */
					val = graph.interpol + pivot + i - 1;
				else
					continue;

				/* display first intersection */
				if (val[0] < val[1] ? val[0] <= y && y <= val[1] : val[1] <= y && y <= val[0])
				{
					float step = 2 * graph.range / graph.width;
					float x1 = graph.curveStartX + (val - graph.interpol) * step;

					sprintf(graph.peekX, "X = %g", (y - val[0]) / (val[1] - val[0]) * step + x1);
					break;
				}
			}
		}
	}
	else graph.peekX[0] = 0;
	SIT_ForceRefresh();
}

/* SITE_OnClick and SITE_OnMouseMove */
static int graphClick(SIT_Widget w, APTR cd, APTR ud)
{
	static int startX, startY, initDX, initDY;
	SIT_OnMouse * msg = cd;
	switch (msg->state) {
	case SITOM_ButtonPressed:
		switch (msg->button) {
		case SITOM_ButtonLeft:
			startX = msg->x;
			startY = msg->y;
			initDX = graph.dx;
			initDY = graph.dy;
			return 2;
		case SITOM_ButtonWheelDown:
			graphZoom(1);
			break;
		case SITOM_ButtonWheelUp:
			graphZoom(-1);
		default: break;
		}
		break;
	case SITOM_Move:
		graph.hover = 1;
		graph.mouseX = msg->x;
		graph.mouseY = msg->y;
		if (graph.peekLine)
			graphSetPeekStr();
		break;
	case SITOM_CaptureMove:
		if (startX > 0)
		{
			graph.dx = initDX + (msg->x - startX);
			graph.dy = initDY + (msg->y - startY);
			graph.refresh = 1;
			SIT_ForceRefresh();
		}
		break;
	case SITOM_ButtonReleased:
		startX = startY = 0;
	}
	return 1;
}

static int graphExit(SIT_Widget w, APTR cd, APTR ud)
{
	graph.hover = 0;
	return 1;
}

void graphSetPeek(int set, Bool vertical)
{
	if (set)
		graphSetPeekStr();

	graph.peekLine = set ? vertical+1 : 0;
	SIT_ForceRefresh();
}

void graphReset(void)
{
	graph.range = 2;
	graph.dx = graph.dy = 0;
	graph.waitConf = 1;
	graph.function[0] = 0;
	graph.refresh = 0;
	graph.peekX[0] = 0;
	graph.peekLine = 0;
}

void graphSetFunc(STRPTR func)
{
	CopyString(graph.function, func, sizeof graph.function);
	graph.refresh = 1;

	int len = strlen(graph.function) + 1;
	if (len > 1)
	{
		DATA8 mem = configAddChunk("_GRAPH", len);

		if (mem) memcpy(mem, graph.function, len);
	}
	else configDelChunk("_GRAPH");
}

STRPTR graphGetFunc(void)
{
	return graph.function;
}

void graphInit(SIT_Widget canvas)
{
	graph.canvas = canvas;
	graphReset();
	SIT_AddCallback(canvas, SITE_OnResize,    graphResize, NULL);
	SIT_AddCallback(canvas, SITE_OnPaint,     graphPaint,  NULL);
	SIT_AddCallback(canvas, SITE_OnClickMove, graphClick,  NULL);
	SIT_AddCallback(canvas, SITE_OnMouseOut,  graphExit,   NULL);
}
