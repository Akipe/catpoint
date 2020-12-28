/* See LICENSE file for license details. */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <curses.h>
#include <fcntl.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *currentslidep, **slidefiles; /* the slides */
int nslides, currentslide, currentslidelen, exitstatus = 1;

volatile sig_atomic_t slidechanged = 1;

void
unloadcurrentslide(void)
{
	if (currentslidep == NULL)
		return;

	if (munmap(currentslidep, currentslidelen) < 0)
		err(1, "munmap: %s", slidefiles[currentslide]);
}

void
cleanup(int sig)
{
	unloadcurrentslide();

	endwin(); /* restore terminal */
	exit(exitstatus);
}

void
loadcurrentslide(char **argv, int slide)
{
	struct stat statbuf;
	int fd;

	unloadcurrentslide();

	fd = open(slidefiles[slide], O_RDONLY, 0);
	if (fd < 0)
		err(1, "open: %s", slidefiles[slide]);
	if (fstat(fd, &statbuf) < 0)
		err(1, "fstat: %s", slidefiles[slide]);
	currentslidep = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (currentslidep == MAP_FAILED)
		err(1, "mmap");
	currentslidelen = statbuf.st_size;
	close(fd);
}

void
reloadcurrentslide(int sig)
{
	if (sig == SIGHUP) {
		/* Make ncurses redisplay slide. */
		if (raise(SIGWINCH) < 0)
			err(1, "raise");
	}

	/*
	 * Keep this out of SIGHUP, in case this is used somewhere else.
	 */
	slidechanged = 1;
}

void
setsignal()
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	sa.sa_handler = cleanup;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	sa.sa_handler = reloadcurrentslide;
	sigaction(SIGHUP, &sa, NULL);
}

int
main(int argc, char *argv[])
{
	int c;

	if (argc == 1)
		errx(1, "usage: %s file ...", argv[0]);
	slidefiles = ++argv;
	nslides = --argc;

	setsignal();
	setlocale(LC_ALL, "");

	/* start */
	currentslide = 0;
	currentslidep = NULL;
	currentslidelen = 0;

	/* init curses */
	initscr();
	cbreak();
	noecho();
	nonl();
	intrflush(stdscr, FALSE);
	keypad(stdscr, TRUE);
	curs_set(FALSE); /* hide cursor */

show:
	/* display slide if changed */
	if (slidechanged) {
		slidechanged = 0;
		loadcurrentslide(slidefiles, currentslide);
	}
	clear();
	refresh();
	printw("%.*s", currentslidelen, currentslidep);

again:
	c = getch();
	switch (c) {
	/* powerpoint remote presenter shortcuts */
	case 4: /* ^D, EOT */
	case 27:
	case KEY_F(5):
	/* end presentation */
	case 'q':
		break;
	/* next */
	case 'l':
	case 'j':
	case KEY_RIGHT:
	case KEY_DOWN:
	case KEY_NPAGE:
		if (currentslide < nslides - 1) {
			slidechanged = 1;
			currentslide++;
			goto show;
		}
		goto again;
	/* prev */
	case ' ':
	case 'h':
	case 'k':
	case KEY_LEFT:
	case KEY_UP:
	case KEY_PPAGE:
		if (currentslide > 0) {
			slidechanged = 1;
			currentslide--;
			goto show;
		}
		goto again;
	/* shortcut from powerpoint. Needed for remote presenters. */
	case '.':
	/* first */
	case 'u':
	case KEY_BEG:
	case KEY_HOME:
		if (currentslide != 0)
			slidechanged = 1;
		currentslide = 0;
		goto show;
	/* last */
	case 'i':
	case KEY_END:
		if (currentslide != (nslides - 1))
			slidechanged = 1;
		currentslide = nslides - 1;
		goto show;
	/* reload */
	case 'r':
	case 12: /* ^L, redraw */
	case KEY_RESIZE: /* resize / SIGWINCH */
		goto show;
	default:
		/* printf("key pressed = '%d'\n", c); */
		goto again;
	}

	/* unmap mem */
	exitstatus = 0;
	cleanup(0);

	return 0;
}

