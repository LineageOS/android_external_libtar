/*
**  Copyright 1998-2003 University of Illinois Board of Trustees
**  Copyright 1998-2003 Mark D. Roth
**  All rights reserved.
**
**  block.c - libtar code to handle tar archive header blocks
**
**  Mark D. Roth <roth@uiuc.edu>
**  Campus Information Technologies and Educational Services
**  University of Illinois at Urbana-Champaign
*/

#include <internal.h>

#include <errno.h>

#ifdef STDC_HEADERS
# include <string.h>
# include <stdlib.h>
# include <stdio.h>
#endif


#define BIT_ISSET(bitmask, bit) ((bitmask) & (bit))

// Used to identify selinux_context in extended ('x')
// metadata. From RedHat implementation.
#define SELINUX_TAG "RHT.security.selinux="
#define SELINUX_TAG_LEN 21

/* read a header block */
int
th_read_internal(TAR *t)
{
	int i;
	int num_zero_blocks = 0;

	DBGMSG("==> th_read_internal(TAR=\"%s\")\n", t->pathname);

	while ((i = tar_block_read(t, &(t->th_buf))) == T_BLOCKSIZE)
	{
		/* two all-zero blocks mark EOF */
		if (t->th_buf.name[0] == '\0')
		{
			num_zero_blocks++;
			if (!BIT_ISSET(t->options, TAR_IGNORE_EOT)
			    && num_zero_blocks >= 2)
				return 0;	/* EOF */
			else
				continue;
		}

		/* verify magic and version */
		if (BIT_ISSET(t->options, TAR_CHECK_MAGIC)
		    && strncmp(t->th_buf.magic, TMAGIC, TMAGLEN - 1) != 0)
		{
			perror("!!! unknown magic value in tar header");
			return -2;
		}

		if (BIT_ISSET(t->options, TAR_CHECK_VERSION)
		    && strncmp(t->th_buf.version, TVERSION, TVERSLEN) != 0)
		{
			perror("!!! unknown version value in tar header");
			return -2;
		}

		/* check chksum */
		if (!BIT_ISSET(t->options, TAR_IGNORE_CRC)
		    && !th_crc_ok(t))
		{
			perror("!!! tar header checksum error");
			return -2;
		}

		break;
	}

	DBGMSG("<== th_read_internal(): returning %d\n", i);
	return i;
}


/* wrapper function for th_read_internal() to handle GNU extensions */
int
th_read(TAR *t)
{
	int i, j;
	size_t sz;
	char *ptr;

	DBGMSG("==> th_read(t=0x%p)\n", t);

	if (t->th_buf.gnu_longname != NULL)
		free(t->th_buf.gnu_longname);
	if (t->th_buf.gnu_longlink != NULL)
		free(t->th_buf.gnu_longlink);
#ifdef HAVE_SELINUX
	if (t->th_buf.selinux_context != NULL)
		free(t->th_buf.selinux_context);
#endif

	memset(&(t->th_buf), 0, sizeof(struct tar_header));

	i = th_read_internal(t);
	if (i == 0)
		return 1;
	else if (i != T_BLOCKSIZE)
	{
		if (i != -1)
			errno = EINVAL;
		return -1;
	}

	/* check for GNU long link extention */
	if (TH_ISLONGLINK(t))
	{
		sz = th_get_size(t);
		j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0);
		DBGMSG("    th_read(): GNU long linkname detected "
		       "(%u bytes, %d blocks)\n", sz, j);
		t->th_buf.gnu_longlink = (char *)malloc(j * T_BLOCKSIZE);
		if (t->th_buf.gnu_longlink == NULL)
			return -1;

		for (ptr = t->th_buf.gnu_longlink; j > 0;
		     j--, ptr += T_BLOCKSIZE)
		{
			DBGMSG("    th_read(): reading long linkname "
			       "(%u blocks left, ptr == 0x%p)\n", j, ptr);
			i = tar_block_read(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
			DBGMSG("    th_read(): read block == \"%s\"\n", ptr);
		}
		DBGMSG("    th_read(): t->th_buf.gnu_longlink == \"%s\"\n",
		       t->th_buf.gnu_longlink);

		i = th_read_internal(t);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}
	}

	/* check for GNU long name extention */
	if (TH_ISLONGNAME(t))
	{
		sz = th_get_size(t);
		j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0);
		DBGMSG("    th_read(): GNU long filename detected "
		       "(%u bytes, %d blocks)\n", sz, j);
		t->th_buf.gnu_longname = (char *)malloc(j * T_BLOCKSIZE);
		if (t->th_buf.gnu_longname == NULL)
			return -1;

		for (ptr = t->th_buf.gnu_longname; j > 0;
		     j--, ptr += T_BLOCKSIZE)
		{
			DBGMSG("    th_read(): reading long filename "
			       "(%d blocks left, ptr == 0x%p)\n", j, ptr);
			i = tar_block_read(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
			DBGMSG("    th_read(): read block == \"%s\"\n", ptr);
		}
		DBGMSG("    th_read(): t->th_buf.gnu_longname == \"%s\"\n",
		       t->th_buf.gnu_longname);

		i = th_read_internal(t);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}
	}

#ifdef HAVE_SELINUX
	if(TH_ISEXTHEADER(t))
	{
		sz = th_get_size(t);

		if(sz >= T_BLOCKSIZE) // Not supported
		{
			DBGMSG("    th_read(): Extended header is too long!\n");
		}
		else
		{
			char buf[T_BLOCKSIZE];
			i = tar_block_read(t, buf);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}

			// To be sure
			buf[T_BLOCKSIZE-1] = 0;

			int len = strlen(buf);
			char *start = strstr(buf, SELINUX_TAG);
			if(start && start+SELINUX_TAG_LEN < buf+len)
			{
				start += SELINUX_TAG_LEN;
				char *end = strchr(start, '\n');
				if(end)
				{
					t->th_buf.selinux_context = strndup(start, end-start);
					DBGMSG("    th_read(): SELinux context xattr detected: %s\n", t->th_buf.selinux_context);
				}
			}
		}

		i = th_read_internal(t);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}
	}
#endif

	return 0;
}


/* write a header block */
int
th_write(TAR *t)
{
	int i, j;
	char type2;
	size_t sz, sz2;
	char *ptr;
	char buf[T_BLOCKSIZE];

	DBGMSG("==> th_write(TAR=\"%s\")\n", t->pathname);
	th_debug(t);

	if ((t->options & TAR_GNU) && t->th_buf.gnu_longlink != NULL)
	{
		DBGMSG("th_write(): using gnu_longlink (\"%s\")\n",
		       t->th_buf.gnu_longlink);
		/* save old size and type */
		type2 = t->th_buf.typeflag;
		sz2 = th_get_size(t);

		/* write out initial header block with fake size and type */
		t->th_buf.typeflag = GNU_LONGLINK_TYPE;
		sz = strlen(t->th_buf.gnu_longlink);
		th_set_size(t, sz);
		th_finish(t);
		i = tar_block_write(t, &(t->th_buf));
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* write out extra blocks containing long name */
		for (j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0),
		     ptr = t->th_buf.gnu_longlink; j > 1;
		     j--, ptr += T_BLOCKSIZE)
		{
			i = tar_block_write(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
		}
		memset(buf, 0, T_BLOCKSIZE);
		strncpy(buf, ptr, T_BLOCKSIZE);
		i = tar_block_write(t, &buf);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* reset type and size to original values */
		t->th_buf.typeflag = type2;
		th_set_size(t, sz2);
	}

	if ((t->options & TAR_GNU) && t->th_buf.gnu_longname != NULL)
	{
		DBGMSG("th_write(): using gnu_longname (\"%s\")\n",
		       t->th_buf.gnu_longname);
		/* save old size and type */
		type2 = t->th_buf.typeflag;
		sz2 = th_get_size(t);

		/* write out initial header block with fake size and type */
		t->th_buf.typeflag = GNU_LONGNAME_TYPE;
		sz = strlen(t->th_buf.gnu_longname);
		th_set_size(t, sz);
		th_finish(t);
		i = tar_block_write(t, &(t->th_buf));
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* write out extra blocks containing long name */
		for (j = (sz / T_BLOCKSIZE) + (sz % T_BLOCKSIZE ? 1 : 0),
		     ptr = t->th_buf.gnu_longname; j > 1;
		     j--, ptr += T_BLOCKSIZE)
		{
			i = tar_block_write(t, ptr);
			if (i != T_BLOCKSIZE)
			{
				if (i != -1)
					errno = EINVAL;
				return -1;
			}
		}
		memset(buf, 0, T_BLOCKSIZE);
		strncpy(buf, ptr, T_BLOCKSIZE);
		i = tar_block_write(t, &buf);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* reset type and size to original values */
		t->th_buf.typeflag = type2;
		th_set_size(t, sz2);
	}

#ifdef HAVE_SELINUX
	if((t->options & TAR_STORE_SELINUX) && t->th_buf.selinux_context != NULL)
	{
		DBGMSG("th_write(): using selinux_context (\"%s\")\n",
		       t->th_buf.selinux_context);
		/* save old size and type */
		type2 = t->th_buf.typeflag;
		sz2 = th_get_size(t);

		/* write out initial header block with fake size and type */
		t->th_buf.typeflag = TH_EXT_TYPE;

		/* setup size - EXT header has format "*size of this whole tag as ascii numbers* *space* *content* *newline* */
		//                                                       size   newline
		sz = SELINUX_TAG_LEN + strlen(t->th_buf.selinux_context) + 3  +    1;

		if(sz >= 100) // another ascci digit for size
			++sz;

		if(sz >= T_BLOCKSIZE) // impossible
		{
			errno = EINVAL;
			return -1;
		}

		th_set_size(t, sz);
		th_finish(t);
		i = tar_block_write(t, &(t->th_buf));
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		memset(buf, 0, T_BLOCKSIZE);
		snprintf(buf, T_BLOCKSIZE, "%d "SELINUX_TAG"%s\n", sz, t->th_buf.selinux_context);
		i = tar_block_write(t, &buf);
		if (i != T_BLOCKSIZE)
		{
			if (i != -1)
				errno = EINVAL;
			return -1;
		}

		/* reset type and size to original values */
		t->th_buf.typeflag = type2;
		th_set_size(t, sz2);
	}
#endif

	th_finish(t);

	/* print tar header */
	th_debug(t);

	i = tar_block_write(t, &(t->th_buf));
	if (i != T_BLOCKSIZE)
	{
		if (i != -1)
			errno = EINVAL;
		return -1;
	}

	DBGMSG("th_write(): returning 0\n");
	return 0;
}


