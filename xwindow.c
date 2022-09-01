/* See LICENSE file for copyright and license details. */
#include <sys/types.h>
#include <sys/time.h>
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
#include <time.h>
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
#define LENGTH(X)         (sizeof(X) / sizeof(X)[0])
#define TEXTW(X)          (drw_fontset_getwidth(drw, (X)) + lrpad)

enum { SchemeNorm, SchemeBar }; /* color schemes */
enum { WMDelete, WMName, WMLast }; /* atoms */

/* Purely graphic info */
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
static void drawbar(void);

static void quit(const Arg *arg);
static void togglebar(const Arg *arg);

/* X events */
static void bpress(XEvent *);
static void cmessage(XEvent *);
static void expose(XEvent *);
static void kpress(XEvent *);
static void configure(XEvent *);

/* variables */
static Atom atoms[WMLast];
static Clr **scheme;
static Drw *drw;
static Display *dpy;
static Window win;
static char right[128], left[128]; /* bar buf */
static int bh = 0;      /* bar geometry */
static int running = 1;
static int lrpad;       /* sum of left and right padding for text */
static int winw, winh;  /* window size */
static int winy;        /* window height - bar height */

/* config.h for applying patches and the configuration. */
#include "config.h"

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
	XDestroyWindow(dpy, win);
	XSync(dpy, False);
	XCloseDisplay(dpy);
}

void
quit(const Arg *arg)
{
	running = 0;
}

void
togglebar(const Arg *arg)
{
	showbar = !showbar;
	if (showbar) {
		winh -= bh;
		printf("BAR IS ON\n");
	} else {
		winh += bh;
		printf("bar is off\n");
	}
	XClearWindow(dpy, win);
	XSync(dpy, False);
	drawbar();
}

void
drawbar(void)
{
	int y, tw = 0;

	if (!showbar)
		return;

	/* currently topbar is not supported */
	y = winh - bh;

	drw_setscheme(drw, scheme[SchemeBar]);

	/* left text */
	struct timeval tv;
	time_t t;
	struct tm *info;
	char buffer[64];

	gettimeofday(&tv, NULL);
	t = tv.tv_sec;
	info = localtime(&t);
	strftime(buffer, sizeof(buffer), "Today is %A, %B %d..", info);

	snprintf(left, LENGTH(left), "%s", buffer);
	drw_text(drw, 0, y, winw / 2, bh, lrpad / 2, left, 0);

	/* right text */
	char str[] = "..And is a good day!";
	snprintf(right, LENGTH(right), "%s", str);
	tw = TEXTW(right) - lrpad + 2; /* 2px right padding */
	drw_text(drw, winw/2, y, winw/2, bh, winw/2 - (tw + lrpad / 2), right, 0);

	drw_map(drw, win, 0, y, winw, bh);
}

void
run(void)
{
	XEvent ev;

	/* main event loop */
	XSync(dpy, False);
	while (running) {
		XNextEvent(dpy, &ev);
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
	sizeh->height = winh;
	sizeh->width = winw;

	XSetWMProperties(dpy, win, NULL, NULL, NULL, 0, sizeh, &wm, &class);
	XFree(sizeh);
}

void
setup(void)
{
	XTextProperty prop;
	XSetWindowAttributes attrs;
	unsigned int i;
	Visual *visual;
	int screen;

	if (!(dpy = XOpenDisplay(NULL)))
		die("xwindow: Unable to open display");

	/* init screen */
	screen = XDefaultScreen(dpy);
	visual = XDefaultVisual(dpy, screen);

	if (!winw)
		winw = winwidth;
	if (!winh)
		winh = winheight;

	attrs.bit_gravity = CenterGravity;
	attrs.event_mask = KeyPressMask | ExposureMask | StructureNotifyMask |
	                      ButtonMotionMask | ButtonPressMask;

	/* init window */
	win = XCreateWindow(dpy, XRootWindow(dpy, screen), 0, 0,
		winw, winh, 0, XDefaultDepth(dpy, screen), InputOutput,
		visual, CWBitGravity | CWEventMask, &attrs);

	/* init atoms */
	atoms[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	atoms[WMName]   = XInternAtom(dpy, "_NET_WM_NAME", False);
	XSetWMProtocols(dpy, win, &atoms[WMDelete], 1);

	if (!(drw = drw_create(dpy, screen, win, winw, winh)))
		die("xwindow: Unable to create drawing context");

	/* init appearance */
	scheme = ecalloc(LENGTH(colors), sizeof(Clr *));
	for (i = 0; i < LENGTH(colors); i++)
		scheme[i] = drw_scm_create(drw, colors[i], 2);

	XSetWindowBackground(dpy, win, scheme[SchemeNorm][ColBg].pixel);

	/* init fonts */
	if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
		die("no fonts could be loaded.");
	lrpad = drw->fonts->h;
	bh = drw->fonts->h + 2; /* two pixel padding */

	/* init bar */
	drawbar();

	XStringListToTextProperty(&argv0, 1, &prop);
	XSetWMName(dpy, win, &prop);
	XSetTextProperty(dpy, win, &prop, atoms[WMName]);
	XFree(prop.value);
	XMapWindow(dpy, win);
	xhints();
}

void
bpress(XEvent *e)
{
	unsigned int i;

	printf("\tmouse 'Button%d'\n", e->xbutton.button);
	for (i = 0; i < LENGTH(mshortcuts); i++)
		if (e->xbutton.button == mshortcuts[i].b && mshortcuts[i].func)
			mshortcuts[i].func(&(mshortcuts[i].arg));
}

void
cmessage(XEvent *e)
{
	printf("XEVENT ClientMessage\n");
	if (e->xclient.data.l[0] == atoms[WMDelete]) {
		printf("Handling ClientMessage, closing window..\n");
		running = 0;
	}
}

void
expose(XEvent *e)
{
	printf("XEVENT Expose\n");
	if (0 == e->xexpose.count) {
		printf("Handling expose '%d'\n", e->xexpose.count);
		XClearWindow(dpy, win);
		drawbar();
	}
}

void
kpress(XEvent *e)
{
	unsigned int i;
	KeySym sym;

	sym = XkbKeycodeToKeysym(dpy, (KeyCode)e->xkey.keycode, 0, 0);
	printf("\tkeypress '%s'\n", XKeysymToString(sym));
	for (i = 0; i < LENGTH(shortcuts); i++)
		if (sym == shortcuts[i].keysym && shortcuts[i].func)
			shortcuts[i].func(&(shortcuts[i].arg));
}

void
configure(XEvent *e)
{
	XConfigureEvent *ev = &e->xconfigure;
	printf("Configure\n");

	if (winw != ev->width || winh != ev->height) {
		printf("Handling configurenotify (width: '%d', height: '%d')\n", ev->width, ev->height);
		winw = ev->width;
		winh = ev->height;
		drw_resize(drw, winw, winh + bh);
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
		die("xwindow-"VERSION);
		break;
	default:
		usage();
		break;
	} ARGEND

	setup();
	run();
	cleanup();

	return EXIT_SUCCESS;
}
