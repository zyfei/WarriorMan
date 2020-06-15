#include "file.h"

int wm_tmpfile(char *filename) {
#if defined(HAVE_MKOSTEMP) && defined(HAVE_EPOLL)
	int tmp_fd = mkostemp(filename, O_WRONLY | O_CREAT);
#else
	int tmp_fd = mkstemp(filename);
#endif
	if (tmp_fd < 0) {
		wmWarn("mkstemp(%s) failed", filename);
		return -1;
	} else {
		return tmp_fd;
	}
}

long wm_file_get_size(FILE *fp) {
	long pos = ftell(fp);
	if (fseek(fp, 0L, SEEK_END) < 0) {
		return -1;
	}
	long size = ftell(fp);
	if (fseek(fp, pos, SEEK_SET) < 0) {
		return -1;
	}
	return size;
}

long wm_file_size(const char *filename) {
	struct stat file_stat;
	if (lstat(filename, &file_stat) < 0) {
		wmWarn("lstat(%s) failed", filename);
		return -1;
	}
	if ((file_stat.st_mode & S_IFMT) != S_IFREG) {
		wmWarn("file_mode not a S_IFREG (%s) failed", filename);
		return -1;
	}
	return file_stat.st_size;
}

wmString* wm_file_get_contents(const char *filename) {
	long filesize = wm_file_size(filename);
	if (filesize < 0) {
		return NULL;
	} else if (filesize == 0) {
		wmTrace("file[%s] is empty", filename);
		return NULL;
	} else if (filesize > WM_MAX_FILE_CONTENT) {
		wmWarn("file[%s] is too large", filename);
		return NULL;
	}

	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		wmWarn("open(%s) failed", filename);
		return NULL;
	}
	wmString *content = wmString_new(filesize);
	if (!content) {
		close(fd);
		return NULL;
	}

	int readn = 0;
	int n;

	while (readn < filesize) {
		n = pread(fd, content->str + readn, filesize - readn, readn);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				wmWarn("pread(%d, %ld, %d) failed", fd, filesize - readn, readn);
				wmString_free(content);
				close(fd);
				return NULL;
			}
		}
		readn += n;
	}
	close(fd);
	content->length = readn;
	return content;
}

int wm_file_put_contents(const char *filename, const char *content, size_t length, bool append) {
	if (length <= 0) {
		wmTrace("wm_file_put_contents(%s) content is empty", filename);
		return false;
	}
	if (length > WM_MAX_FILE_CONTENT) {
		wmWarn("wm_file_put_contents(%s) content is too large", filename);
		return false;
	}

	int flags = O_WRONLY | O_CREAT;
	if (append) {
		flags = flags | O_APPEND;
	} else {
		flags = flags | O_TRUNC;
	}

	int fd = open(filename, flags, 0666);
	if (fd < 0) {
		wmWarn("open(%s) failed", filename);
		return false;
	}

	int n, chunk_size, written = 0;

	while (written < length) {
		chunk_size = length - written;
		if (chunk_size > WM_BUFFER_SIZE_BIG) {
			chunk_size = WM_BUFFER_SIZE_BIG;
		}
		n = write(fd, content + written, chunk_size);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			} else {
				wmWarn("write(%d, %d) failed", fd, chunk_size);
				close(fd);
				return false;
			}
		}
		written += n;
	}
	close(fd);
	return true;
}
