#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ftw.h>
#include <utime.h>

#define EXT2_IMMUTABLE_FL 0x00000010
#define BACKUP_DIR "/big"

#define MAX_DIRS_OPEN 20

char *backup_dir = BACKUP_DIR;

void
usage (void)
{
	printf ("usage: bakim [FILE]...\n");
}

int fsetflags (const char * name, unsigned long flags)
{
	int fd, r, f, save_errno = 0;
	struct stat buf;

	if (!lstat(name, &buf) &&
	    !S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode)) {
		goto notsupp;
	}

	fd = open (name, O_RDONLY);
	if (fd == -1)
		return -1;
	f = (int) flags;
	r = ioctl (fd, _IOW('f', 2, long), &f);
	if (r == -1)
		save_errno = errno;
	close (fd);
	if (save_errno)
		errno = save_errno;
	return r;

notsupp:
	return -1;
}

int fgetflags (const char * name, unsigned long * flags)
{
	struct stat buf;

	int fd, r, f;

	if (!lstat(name, &buf) &&
	    !S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode)) {
		goto notsupp;
	}
	fd = open (name, O_RDONLY);
	if (fd == -1)
		return -1;
	r = ioctl (fd, _IOR ('f', 1, long), &f);
	*flags = f;
	close (fd);
	return r;

notsupp:
	return -1;
}

static int set_immutable (const char *fn)
{
	unsigned long flags;

	if (fgetflags(fn, &flags) == -1) {
		printf ("failed to get flags for %s\n", fn);
		return -1;
	}

	flags |= EXT2_IMMUTABLE_FL;

	if (fsetflags(fn, flags) == -1) {
		printf ("failed to set flags for %s\n", fn);
		return -1;
	}
	return 0;
}

static int
mk_backup (const char *path, const struct stat *sb,
		int tflag, struct FTW *ftwbuf)
{
	char buf[1024*1024], dst_name[PATH_MAX], lnk_tar[PATH_MAX];
	struct utimbuf times;
	FILE *src, *dst;
	int n_read, r;

	switch (tflag) {
	case FTW_F:
		if ((src = fopen (path, "r")) == NULL) {
			printf ("cannot open src file %s\n", path);
			exit (1);
		}

		// add 100 for a safe buffer
		if (strlen (path) + strlen (backup_dir) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dst_name, "%s/%s", backup_dir, path);

		if ((dst = fopen (dst_name, "w")) == NULL) {
			fprintf (stderr, "cannot open dst file %s\n", dst_name);
			exit (1);
		}

		while ((n_read = fread (buf, 1, sizeof buf, src)) > 0) {
			if (fwrite (buf, 1, n_read, dst) != n_read) {
				fprintf (stderr, "error copying file:"
					 " potentially out of space\n");
				exit (1);
			}
		}

		if (fclose (src) != 0) {
			fprintf (stderr, "error closing file %s: %m", path);
			exit (1);
		}
		if (fclose (dst) != 0) {
			fprintf (stderr, "error closing file %s: %m", dst_name);
			exit (1);
		}

		times.actime = sb->st_atime;
		times.modtime = sb->st_mtime;

		if (utime (dst_name, &times) == -1) {
			fprintf (stderr, "failed to set timestamps on %s: %m\n",
				 dst_name);
		}

		if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n", dst_name);
		}

		set_immutable (dst_name);
		break;
	case FTW_D:
		// add 100 for a safe buffer
		if (strlen (path) + strlen (backup_dir) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dst_name, "%s/%s", backup_dir, path);

		if (mkdir (dst_name, sb->st_mode) == -1) {
			fprintf (stderr, "failed to create directory %s: %m\n",
				 dst_name);
			exit (1);
		}

		if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n", dst_name);
		}

		break;
	case FTW_SL:
		// add 100 for a safe buffer
		if (strlen (path) + strlen (backup_dir) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dst_name, "%s/%s", backup_dir, path);

		r = readlink (path, lnk_tar, sb->st_size + 1);

		if (r < 0) {
			fprintf (stderr, "failed to read link %s: %m\n",
				 dst_name);
			exit (1);
		}

		if (r > sb->st_size) {
			fprintf (stderr, "symlink increased in size "
				 "between lstat and readlink\n");
			exit (1);
		}

		lnk_tar[sb->st_size] = 0;

		if (symlink (lnk_tar, dst_name) == -1) {
			fprintf (stderr, "failed to create symlink %s: %m\n",
				 dst_name);
			exit (1);
		}

		if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n", dst_name);
		}

		break;
	default:
		return (0);
	}

	return (0);
}

static int
fix_dirs (const char *path, const struct stat *sb,
	  int tflag, struct FTW *ftwbuf)
{
	char dst_name[PATH_MAX];
	struct utimbuf times;

	switch (tflag) {
	case FTW_D:
		// add 100 for a safe buffer
		if (strlen (path) + strlen (backup_dir) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dst_name, "%s/%s", backup_dir, path);

		times.actime = sb->st_atime;
		times.modtime = sb->st_mtime;

		if (utime (dst_name, &times) == -1) {
			fprintf (stderr, "failed to set timestamps on %s: %m\n",
				 dst_name);
		}

		break;
	default:
		break;
	}

	return (0);
}

int
main (int argc, char **argv)
{
	int c, idx, flags;

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	flags = FTW_PHYS;

	if (optind < argc) {
		for (idx = optind; idx < argc; idx++) {
			if (nftw (argv[idx], mk_backup,
				  MAX_DIRS_OPEN, flags) == -1) {
				fprintf (stderr, "nftw failed\n");
				return (-1);
			}
		}

		for (idx = optind; idx < argc; idx++) {
			if (nftw (argv[idx], fix_dirs,
				  MAX_DIRS_OPEN, flags) == -1) {
				fprintf (stderr, "nftw failed\n");
				return (-1);
			}
		}
	} else {
		usage ();
	}


	return (0);
}
