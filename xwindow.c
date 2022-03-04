/* See LICENSE file for copyright and license details. */
#include <sys/types.h>
#include <arpa/inet.h>

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "arg.h"
#include "util.h"
#include "drw.h"

char *argv0;

/* macros */
#define LENGTH(a)         (sizeof(a) / sizeof(a)[0])
#define TEXTW(X)          (drw_fontset_getwidth(drw, (X)) + lrpad)

enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { WMDelete, WMName, WMLast }; /* atoms */

/* Purely graphic info */
typedef struct {
	Display *dpy;
	Window win;
	Visual *vis;
	int scr;
	int w, h;
} XWindow;

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int b;
	void (*func)(const Arg *);
	const Arg arg;
} Mousekey;

typedef struct {
	KeySym keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Shortcut;

static void cleanup(void);
static void run(void);
static void usage(void);
static void xhints(void);
static void setup(void);

static void quit(const Arg *arg);

/* X events */
static void bpress(XEvent *);
static void cmessage(XEvent *);
static void expose(XEvent *);
static void kpress(XEvent *);
static void configure(XEvent *);

/* config.h for applying patches and the configuration. */
#include "config.h"

/* variables */
static Atom atoms[WMLast];
static XWindow xw;
static Drw *drw;
static Clr **scheme;
static int running = 1;
//static int bh, blw = 0;      /* bar geometry */
static int lrpad;            /* sum of left and right padding for text */

static void (*handler[LASTEvent])(XEvent *) = {
	[ButtonPress] = bpress,
	[ClientMessage] = cmessage,
	[ConfigureNotify] = configure,
	[Expose] = expose,
	[KeyPress] = kpress,
};

void
cleanup(void)
{
	unsigned int i;

	for (i = 0; i < LENGTH(colors); i++)
		free(scheme[i]);
	free(scheme);
	drw_free(drw);
	XDestroyWindow(xw.dpy, xw.win);
	XSync(xw.dpy, False);
	XCloseDisplay(xw.dpy);
}

void
quit(const Arg *arg)
{
	running = 0;
}

void
run(void)
{
	XEvent ev;

	/* main event loop */
	while (running) {
		XNextEvent(xw.dpy, &ev);
		if (handler[ev.type])
			handler[ev.type](&ev); /* call handler */
	}
}

void
xhints(void)
{
	XClassHint class = {.res_name = "xwindow", .res_class = "xwindow"};
	XWMHints wm = {.flags = InputHint, .input = True};
	XSizeHints *sizeh = NULL;

	if (!(sizeh = XAllocSizeHints()))
		die("xwindow: Unable to allocate size hints");

	sizeh->flags = PSize;
	sizeh->height = xw.h;
	sizeh->width = xw.w;

	XSetWMProperties(xw.dpy, xw.win, NULL, NULL, NULL, 0, sizeh, &wm, &class);
	XFree(sizeh);
}

void
setup(void)
{
	XTextProperty prop;
	XSetWindowAttributes attrs;
	unsigned int i;

	if (!(xw.dpy = XOpenDisplay(NULL)))
		die("xwindow: Unable to open display");

	xw.scr = XDefaultScreen(xw.dpy);
	xw.vis = XDefaultVisual(xw.dpy, xw.scr);

	if (!xw.w)
		xw.w = winwidth;
	if (!xw.h)
		xw.h = winheight;

	attrs.bit_gravity = CenterGravity;
	attrs.event_mask = KeyPressMask | ExposureMask | StructureNotifyMask |
	                      ButtonMotionMask | ButtonPressMask;

	xw.win = XCreateWindow(xw.dpy, XRootWindow(xw.dpy, xw.scr), 0, 0,
	                       xw.w, xw.h, 0, XDefaultDepth(xw.dpy, xw.scr),
	                       InputOutput, xw.vis, CWBitGravity | CWEventMask,
	                       &attrs);

	atoms[WMDelete] = XInternAtom(xw.dpy, "WM_DELETE_WINDOW", False);
	atoms[WMName]   = XInternAtom(xw.dpy, "_NET_WM_NAME", False);
	XSetWMProtocols(xw.dpy, xw.win, &atoms[WMDelete], 1);

	if (!(drw = drw_create(xw.dpy, xw.scr, xw.win, xw.w, xw.h)))
		die("xwindow: Unable to create drawing context");

	/* init scheme */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 2);

	XSetWindowBackground(xw.dpy, xw.win, scheme[SchemeNorm][ColBg].pixel);

	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;

	XStringListToTextProperty(&argv0, 1, &prop);
	XSetWMName(xw.dpy, xw.win, &prop);
	XSetTextProperty(xw.dpy, xw.win, &prop, atoms[WMName]);
	XFree(prop.value);
	XMapWindow(xw.dpy, xw.win);
	xhints();
	XSync(xw.dpy, False);
}

void
bpress(XEvent *e)
{
	unsigned int i;

	for (i = 0; i < LENGTH(mshortcuts); i++)
		if (e->xbutton.button == mshortcuts[i].b && mshortcuts[i].func)
			mshortcuts[i].func(&(mshortcuts[i].arg));
}

void
cmessage(XEvent *e)
{
	if (e->xclient.data.l[0] == atoms[WMDelete])
		running = 0;
}

void
expose(XEvent *e)
{
	if (0 == e->xexpose.count)
		printf("expose");
}

void
kpress(XEvent *e)
{
	unsigned int i;
	KeySym sym;

	sym = XkbKeycodeToKeysym(xw.dpy, (KeyCode)e->xkey.keycode, 0, 0);
	for (i = 0; i < LENGTH(shortcuts); i++)
		if (sym == shortcuts[i].keysym && shortcuts[i].func)
			shortcuts[i].func(&(shortcuts[i].arg));
}

void
configure(XEvent *e)
{
	XConfigureEvent *ev = &e->xconfigure;
	printf("Configure\n");

	if (xw.w != ev->width || xw.h != ev->height) {
		printf("Handling configurenotify (width: '%d', height: '%d')\n", ev->width, ev->height);
		xw.w = ev->width;
		xw.h = ev->height;
		drw_resize(drw, xw.w, xw.h);
	}
}

void
usage(void)
{
	die("usage: %s [file]", argv0);
}

int
main(int argc, char *argv[])
{
	ARGBEGIN {
	case 'v':
		fprintf(stderr, "xwindow-"VERSION"\n");
		return 0;
	default:
		usage();
	} ARGEND

	setup();

	run();

	cleanup();

	return 0;
}
