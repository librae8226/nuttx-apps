#ifndef __NEURITE_H__
#define __NEURITE_H__

struct neurite_s {
	int (*write)(void *h, char c);
	int (*try_read)(void *h, char *pch);
	void (*deinit)(void *h);
	void *priv;
};

struct neurite_s *neurite_init(void);

#endif /* __NEURITE_H__ */
