/*
 * preload given file to channel.
 * note: with this class we make paging engine
 *
 * Copyright (c) 2012, LiteStack, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <sys/fcntl.h>
#include <assert.h>
#include "src/channels/mount_channel.h"
#include "src/channels/preload.h"

static int disable_preallocation = 0;

void PreloadAllocationDisable()
{
  disable_preallocation = 1;
}

/* return the file source type or ChannelSourceTypeNumber */
enum ChannelSourceType GetChannelSource(const char *name)
{
  struct stat fs;

  assert(name != NULL);

  /* get the file statistics */
  if(stat(name, &fs) < 0) return ChannelRegular;

  /* calculate the file source type */
  if(S_ISREG(fs.st_mode)) return ChannelRegular;
  if(S_ISDIR(fs.st_mode)) return ChannelDirectory;
  if(S_ISCHR(fs.st_mode)) return ChannelCharacter;
  if(S_ISBLK(fs.st_mode)) return ChannelBlock;
  if(S_ISFIFO(fs.st_mode)) return ChannelFIFO;
  if(S_ISLNK(fs.st_mode)) return ChannelLink;
  if(S_ISSOCK(fs.st_mode)) return ChannelSocket;

  ZLOGFAIL(1, EFAULT, "cannot detect source type of %s", name);
  return ChannelRegular; /* not reachable */
}

/* (adjust and) close file associated with the channel */
int PreloadChannelDtor(struct ChannelDesc* channel)
{
  int i = 0;

  assert(channel != NULL);

  /* adjust the size of writable channels */
  if(channel->limits[PutSizeLimit] && channel->limits[PutsLimit])
    i = ftruncate(channel->handle, channel->putpos);

  /* calculate digest and free the tag */
  if(TagEngineEnabled())
  {
    TagDigest(channel->tag, channel->digest);
    TagDtor(channel->tag);
  }

  ZLOGS(LOG_DEBUG, "channel %s closed with tag = %s, getsize = %ld, "
      "putsize = %ld", channel->alias, channel->digest,
      channel->counters[GetSizeLimit], channel->counters[PutSizeLimit]);

  close(channel->handle);
  return i == 0 ? 0 : -1;
}

/* test the channel for validity */
static void FailOnInvalidFileChannel(const struct ChannelDesc *channel)
{
  ZLOGFAIL(channel->source < ChannelRegular || channel->source > ChannelSocket,
      EFAULT, "'%s' isn't local file", channel->alias);
  ZLOGFAIL(channel->name[0] != '/', EFAULT, "only absolute path allowed");
  ZLOGFAIL(channel->source == ChannelCharacter
      && (channel->limits[PutsLimit] && channel->limits[GetsLimit]),
      EFAULT, "invalid channel limits");
}

/* preload given character device to channel */
static void CharacterChannel(struct ChannelDesc* channel)
{
  int desc;
  char *mode = NULL;
  int flags;

  assert(channel != NULL);

  /* calculate open mode */
  if(channel->limits[PutsLimit] == 0)
  {
    mode = "rb";
    flags = O_RDONLY;
  }
  else
  {
    mode = "wb";
    flags = O_RDWR;
  }

  /* open file */
  desc = open(channel->name, flags | O_NONBLOCK);
  channel->socket = fdopen(desc, mode);
  ZLOGFAIL(channel->socket == NULL, errno, "cannot open channel %s", channel->name);

  /* set channel attributes */
  channel->size = 0;
}

/* preload given regular device to channel */
static void RegularChannel(struct ChannelDesc* channel)
{
  uint32_t rw = 0;
  int i;

  assert(channel != NULL);
  assert(channel->name != NULL);

  /* calculate the read/write type */
  rw |= channel->limits[GetsLimit] && channel->limits[GetSizeLimit];
  rw |= (channel->limits[PutsLimit] && channel->limits[PutSizeLimit]) << 1;
  switch(rw)
  {
    case 1: /* read only */
      channel->handle = open(channel->name, O_RDONLY, S_IRWXU);
      ZLOGFAIL(channel->handle == -1, errno, "'%s' open error", channel->name);
      channel->size = GetFileSize((char*)channel->name);
      break;

    case 2: /* write only. existing file will be overwritten */
      channel->handle = open(channel->name, O_WRONLY|O_CREAT|O_TRUNC, S_IRWXU);
      ZLOGFAIL(channel->handle == -1, errno, "'%s' open error", channel->name);
      channel->size = 0;
      if(STREQ(channel->name, DEV_NULL)) break;

      /* preallocate channel space */
      if(!disable_preallocation)
      {
        i = ftruncate(channel->handle, channel->limits[PutSizeLimit]);
        ZLOGFAIL(i == -1, errno, "cannot preallocate '%s' channel", channel->alias);
      }
      break;

    case 3: /* cdr or full random access */
      ZLOGFAIL(channel->type == SGetSPut, EFAULT,
          "sequential channels cannot have r/w access");
      ZLOGFAIL(channel->type == SGetRPut, EFAULT,
          "sequential read / random write channels not supported");

      /* open the file and ensure that putpos is not greater than the file size */
      channel->handle = open(channel->name, O_RDWR | O_CREAT, S_IRWXU);
      ZLOGFAIL(channel->handle == -1, errno, "'%s' open error", channel->name);
      channel->size = GetFileSize(channel->name);
      ZLOGFAIL(channel->putpos > channel->size, EFAULT,
          "'%s' size is less then specified append position", channel->alias);

      /* file does not exist */
      if(channel->size == 0 && !STREQ(channel->name, DEV_NULL))
      {
        /* preallocate channel space */
        if(!disable_preallocation)
        {
          i = ftruncate(channel->handle, channel->limits[PutSizeLimit]);
          ZLOGFAIL(i == -1, errno, "cannot preallocate cdr channel");
        }
        break;
      }

      /* update the start of writing position */
      channel->putpos = channel->type == RGetSPut ? channel->size : 0;
      break;

    case 0: /* inaccessible */
    default: /* unreachable */
      ZLOGFAIL(1, EPROTONOSUPPORT, "the channel '%s' not supported", channel->alias);
      break;
  }

  ZLOGFAIL(channel->handle < 0, EFAULT, "preloaded file open error");
}

/*
 * preload given file to channel.
 * return 0 if success, otherwise negative errcode
 */
int PreloadChannelCtor(struct ChannelDesc* channel)
{
  assert(channel != NULL);
  assert(channel->name != NULL);

  /* check the given channel */
  ZLOGS(LOG_DEBUG, "mounting channel '%s' to '%s'", channel->name, channel->alias);
  FailOnInvalidFileChannel(channel);

  /* set start position */
  channel->getpos = 0;
  channel->putpos = 0;

  switch(channel->source)
  {
    case ChannelRegular:
      RegularChannel(channel);
      break;
    case ChannelCharacter:
    case ChannelFIFO:
    case ChannelSocket:
      CharacterChannel(channel);
      break;
    default:
      ZLOGFAIL(1, EPROTONOSUPPORT, "the channel not supported");
      break;
  }
  return 0;
}
