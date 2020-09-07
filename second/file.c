/*
 *  file.c - Filesystem related interfaces
 *
 *  Copyright (C) 2001, 2002 Ethan Benson
 *
 *  parse_device_path()
 *
 *  Copyright (C) 2001 Colin Walters
 *
 *  Copyright (C) 1999 Benjamin Herrenschmidt
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "ctype.h"
#include "types.h"
#include "stddef.h"
#include "stdlib.h"
#include "file.h"
#include "prom.h"
#include "string.h"
#include "partition.h"
#include "fs.h"
#include "errors.h"
#include "debug.h"

extern char bootdevice[];
int fserrorno;

/* Convert __u32 into std, dotted quad string, leaks like a sive :( */
static char *
ipv4_to_str(__u32 ip)
{
     char *buf = malloc(sizeof("000.000.000.000"));

     sprintf(buf,"%u.%u.%u.%u",
             (ip & 0xff000000) >> 24, (ip & 0x00ff0000) >> 16,
             (ip & 0x0000ff00) >>  8, (ip & 0x000000ff));

     return buf;
}

/* Ensure the string arg is a plausible IPv4 address */
static char * is_valid_ipv4_str(char *str)
{
     int i;
     long tmp;
     __u32 ip = 0;
     char *ptr=str, *endptr;

     if (str == NULL)
          return NULL;

     for (i=0; i<4; i++, ptr = ++endptr) {
          tmp = strtol(ptr, &endptr, 10);
          if ((tmp & 0xff) != tmp)
               return NULL;

          /* If we reach the end of the string but we're not in the 4th octet
           * we have an invalid IP */
          if (*endptr == '\x0' && i!=3)
               return NULL;

          /* If we have anything other than a NULL or '.' we have an invlaid
           * IP */
          if (*endptr != '\x0' && *endptr != '.')
               return NULL;

          ip += (tmp << (24-(i*8)));
     }

     if (ip == 0 || ip == ~0u)
          return NULL;

     return str;
}


/*
 * Copy the string from source to dest until the end of string or comma is seen
 * in the source.
 * Move source and dest pointers respectively.
 * Returns pointer to the start of the string that has just been copied.
 */
static char *
scopy(char **dest, char **source)
{
     char *ret = *dest;

     if (!**source)
	  return NULL;

     while (**source != ',' && **source != '\0')
	  *(*dest)++ = *(*source)++;
     if (**source != '\0')
	  (void)*(*source)++;
     **dest = '\0';
     (void)*(*dest)++;
     return ret;
}

/*
 * Extract all the ipv4 arguments from the bootpath provided and fill result
 * Returns 1 on success, 0 on failure.
 */
static int
extract_ipv4_args(char *imagepath, struct boot_fspec_t *result)
{
     char *tmp, *args, *str, *start;

     args = strrchr(imagepath, ':');
     if (!args)
	  return 1;

     start = args; /* used to see if we read any optional parameters */

     /* The obp-tftp device arguments should be at the end of
      * the argument list.  Skip over any extra arguments (promiscuous,
      * speed, duplex, bootp, rarp).
      */

     tmp = strstr(args, "promiscuous");
     if (tmp && tmp > args)
	  args = tmp + strlen("promiscuous");

     tmp = strstr(args, "speed=");
     if (tmp && tmp > args)
	  args = tmp + strlen("speed=");

     tmp = strstr(args, "duplex=");
     if (tmp && tmp > args)
	  args = tmp + strlen("duplex=");

     tmp = strstr(args, "bootp");
     if (tmp && tmp > args)
	  args = tmp + strlen("bootp");

     tmp = strstr(args, "rarp");
     if (tmp && tmp > args)
	  args = tmp + strlen("rarp");

     if (args != start) /* we read some parameters, so go past the next comma(,) */
	  args = strchr(args, ',');
     if (!args)
	  return 1;

     str = malloc(strlen(args) + 1); /*long enough to hold all strings */
     if (!str)
	  return 0;

     if (args[-1] != ':')
	  args++; /* If comma(,) is not immediately followed by ':' then go past the , */

     /*
      * read the arguments in order: siaddr,filename,ciaddr,giaddr,
      * bootp-retries,tftp-retries,addl_prameters
      */
     result->siaddr = is_valid_ipv4_str(scopy(&str, &args));
     result->file = scopy(&str, &args);
     result->ciaddr = is_valid_ipv4_str(scopy(&str, &args));
     result->giaddr = is_valid_ipv4_str(scopy(&str, &args));
     result->bootp_retries = scopy(&str, &args);
     result->tftp_retries = scopy(&str, &args);
     result->subnetmask = is_valid_ipv4_str(scopy(&str, &args));
     if (*args) {
	  result->addl_params = strdup(args);
	  if (!result->addl_params)
		return 0;
     }
     return 1;
}

/* DHCP options */
enum dhcp_options {
     DHCP_PAD = 0,
     DHCP_NETMASK = 1,
     DHCP_ROUTERS = 3,
     DHCP_END = 255,
};

#define DHCP_COOKIE        0x63825363
#define DHCP_COOKIE_SIZE   4

/*
 * Process the bootp reply packet's vendor extensions.
 * Vendor extensions are detailed in: http://www.faqs.org/rfcs/rfc1084.html
 */
static void
extract_vendor_options(struct bootp_packet *packet, struct boot_fspec_t *result)
{
     int i = 0;
     __u32 cookie;
     __u8 *options = &packet->options[0];

     memcpy(&cookie, &options[i], DHCP_COOKIE_SIZE);

     if (cookie != DHCP_COOKIE) {
          prom_printf("EEEK! cookie is fubar got %08x expected %08x\n",
                      cookie, DHCP_COOKIE);
          return;
     }

     i += DHCP_COOKIE_SIZE;

     /* FIXME: It may be possible to run off the end of a packet here /if/
      *         it's malformed. :( */
     while (options[i] != DHCP_END) {
          __u8 tag = options[i++], len;
          __u32 value = 0;

          if (tag == DHCP_PAD)
               continue;

          len = options[i++];
          /* Clamp the maxium length of the memcpy() to the right size for
           * value. */
          if (len > sizeof(value))
               memcpy(&value, &options[i], sizeof(value));
          else
               memcpy(&value, &options[i], len);

#if DEBUG
{
     DEBUG_F("tag=%2d, len=%2d, data=", tag, len);
     int j;
     for (j=0; j<len; j++)
          prom_printf("%02x", options[i+j]);
     prom_printf("\n");
}
#endif

          switch (tag) {
               case DHCP_NETMASK:
                    if ((result->subnetmask == NULL ||
                         *(result->subnetmask) == '\x0') && value != 0) {
                         result->subnetmask = ipv4_to_str(value);
                         DEBUG_F("Storing %s as subnetmask from options\n",
                                 result->subnetmask);
                    }
                    break;
               case DHCP_ROUTERS:
                    if ((result->giaddr == NULL || *(result->giaddr) == '\x0')
                        && value != 0) {
                         result->giaddr = ipv4_to_str(value);
                         DEBUG_F("Storing %s as gateway from options\n",
                                 result->giaddr);
                    }
                    break;
               }
          i += len;
     }
}

/*
 * Check netinfo for ipv4 parameters and add them to the fspec iff the
 * fspec has no existing value.
 */
static void
extract_netinfo_args(struct boot_fspec_t *result)
{
     struct bootp_packet *packet;

     /* Check to see if we can get the [scyg]iaddr fields from netinfo */
     packet = prom_get_netinfo();
     if (!packet)
          return;

     DEBUG_F("We have a boot packet\n");
     DEBUG_F(" siaddr = <%x>\n", packet->siaddr);
     DEBUG_F(" ciaddr = <%x>\n", packet->ciaddr);
     DEBUG_F(" yiaddr = <%x>\n", packet->yiaddr);
     DEBUG_F(" giaddr = <%x>\n", packet->giaddr);

     /* Try to fallback to yiaddr if ciaddr is empty. Broken? */
     if (packet->ciaddr == 0 && packet->yiaddr != 0)
          packet->ciaddr = packet->yiaddr;

     if ((result->siaddr == NULL || *(result->siaddr) == '\x0')
         && packet->siaddr != 0)
          result->siaddr = ipv4_to_str(packet->siaddr);
     if ((result->ciaddr == NULL || *(result->ciaddr) == '\x0')
         && packet->ciaddr != 0)
          result->ciaddr = ipv4_to_str(packet->ciaddr);
     if ((result->giaddr == NULL || *(result->giaddr) == '\x0')
         && packet->giaddr != 0)
          result->giaddr = ipv4_to_str(packet->giaddr);

     extract_vendor_options(packet, result);

     /* FIXME: Yck! if we /still/ do not have a gateway then "cheat" and use
      *        the server.  This will be okay if the client and server are on
      *        the same IP network, if not then lets hope the server does ICMP
      *        redirections */
     if (result->giaddr == NULL) {
          result->giaddr = ipv4_to_str(packet->siaddr);
          DEBUG_F("Forcing giaddr to siaddr <%s>\n", result->giaddr);
     }
}

/*
 * Extract all the ipv6 arguments from the bootpath provided and fill result
 * Syntax: ipv6,[dhcpv6[=diaddr,]]ciaddr=c_iaddr,giaddr=g_iaddr,siaddr=s_iaddr,
 *      filename=file_name,tftp-retries=tftp_retries,blksize=block_size
 * Returns 1 on success, 0 on failure.
 */
static int
extract_ipv6_args(char *imagepath, struct boot_fspec_t *result)
{
     char *str, *tmp;
     int total_len;

     result->is_ipv6 = 1;

     /* Just allocate the max required size */
     total_len = strlen(imagepath) + 1;
     str = malloc(total_len);
     if (!str)
	return 0;

     if ((tmp = strstr(imagepath, "dhcpv6=")) != NULL)
	result->dhcpv6 = scopy(&str, &tmp);

     if ((tmp = strstr(imagepath, "ciaddr=")) != NULL)
	result->ciaddr = scopy(&str, &tmp);

     if ((tmp = strstr(imagepath, "giaddr=")) != NULL)
	result->giaddr = scopy(&str, &tmp);

     if ((tmp = strstr(imagepath, "siaddr=")) != NULL)
	result->siaddr = scopy(&str, &tmp);

     if ((tmp = strstr(imagepath, "filename=")) != NULL)
	result->file = scopy(&str, &tmp);

     if ((tmp = strstr(imagepath, "tftp-retries=")) != NULL)
	result->tftp_retries = scopy(&str, &tmp);

     if ((tmp = strstr(imagepath, "blksize=")) != NULL)
	result->blksize = scopy(&str, &tmp);

     return 1;
}

/*
 * Extract all the arguments provided in the imagepath and fill it in result.
 * Returns 1 on success, 0 on failure.
 */
static int
extract_netboot_args(char *imagepath, struct boot_fspec_t *result)
{
     int ret;

     DEBUG_F("imagepath = %s\n", imagepath);

     if (!imagepath)
	  return 1;

     if (strstr(imagepath, TOK_IPV6))
	  ret = extract_ipv6_args(imagepath, result);
     else
	  ret = extract_ipv4_args(imagepath, result);
     extract_netinfo_args(result);

     DEBUG_F("ipv6 = <%d>\n", result->is_ipv6);
     DEBUG_F("siaddr = <%s>\n", result->siaddr);
     DEBUG_F("file = <%s>\n", result->file);
     DEBUG_F("ciaddr = <%s>\n", result->ciaddr);
     DEBUG_F("giaddr = <%s>\n", result->giaddr);
     DEBUG_F("bootp_retries = <%s>\n", result->bootp_retries);
     DEBUG_F("tftp_retries = <%s>\n", result->tftp_retries);
     DEBUG_F("addl_params = <%s>\n", result->addl_params);
     DEBUG_F("dhcpv6 = <%s>\n", result->dhcpv6);
     DEBUG_F("blksize = <%s>\n", result->blksize);

     return ret;
}

static char *netdev_path_to_dev(const char *path)
{
     char *dev, *tmp;
     size_t len;

     DEBUG_F("path = %s\n", path);

     if (!path)
	  return NULL;

     tmp = strchr(path, ':');
     if (!tmp)
	  return strdup(path);
     tmp++;

     len = tmp - path + 1;

     dev = malloc(len);
     if (dev) {
	  strncpy(dev, path, len);
	  dev[len - 1] = '\0';
     }
     return dev;
}

/* This function follows the device path in the devtree and separates
   the device name, partition number, and other datas (mostly file name)
   the string passed in parameters is changed since 0 are put in place
   of some separators to terminate the various strings.

   when a default device is supplied imagepath will be assumed to be a
   plain filename unless it contains a : otherwise if defaultdev is
   NULL imagepath will be assumed to be a device path.

   returns 1 on success 0 on failure.

   Supported examples:
    - /pci@80000000/pci-bridge@d/ADPT,2930CU@2/@1:4
    - /pci@80000000/pci-bridge@d/ADPT,2930CU@2/@1:4,/boot/vmlinux
    - hd:3,/boot/vmlinux
    - enet:10.0.0.1,/tftpboot/vmlinux
    - enet:,/tftpboot/vmlinux
    - enet:bootp
    - enet:0
    - arguments for obp-tftp open as specified in section 4.1 of
      http://playground.sun.com/1275/practice/obp-tftp/tftp1_0.pdf
      [bootp,]siaddr,filename,ciaddr,giaddr,bootp-retries,tftp-retries
      ex: enet:bootp,10.0.0.11,bootme,10.0.0.12,10.0.0.1,5,5
   Supported only if defdevice == NULL
    - disc
    - any other device path lacking a :
   Unsupported examples:
    - hd:2,\\:tbxi <- no filename will be detected due to the extra :
    - enet:192.168.2.1,bootme,c-iaddr,g-iaddr,subnet-mask,bootp-retries,tftp-retries */

int
parse_device_path(char *imagepath, char *defdevice, int defpart,
		  char *deffile, struct boot_fspec_t *result)
{
     char *ptr;
     char *ipath = NULL;
     char *defdev = NULL;
     int device_kind = -1;

     DEBUG_F("imagepath = %s; defdevice %s; defpart %d, deffile %s\n",
		imagepath, defdevice, defpart, deffile);

     result->dev = NULL;
     result->part = -1;
     result->file = NULL;

     if (!imagepath)
	  return 0;

      /*
       * Do preliminary checking for an iscsi device; it may appear as
       * pure a network device (device_type == "network") if this is
       * ISWI.  This is the case on IBM systems doing an iscsi OFW
       * boot.
       */
     if (strstr(imagepath, TOK_ISCSI)) {
 	  /*
	   * get the virtual device information from the
	   * "nas-bootdevice" property.
	   */
 	  if (prom_get_chosen("nas-bootdevice", bootdevice, BOOTDEVSZ)) {
	       DEBUG_F("reset boot-device to"
		       " /chosen/nas-bootdevice = %s\n", bootdevice);
	       device_kind = FILE_DEVICE_ISCSI;
	       ipath = strdup(bootdevice);
	       if (!ipath)
		    return 0;
 	  }
 	  else
 	       return 0;
     }
     else if (!(ipath = strdup(imagepath)))
	  return 0;

     if (defdevice) {
	  defdev = strdup(defdevice);
	  device_kind = prom_get_devtype(defdev);
     } else if (device_kind == -1)
	  device_kind = prom_get_devtype(ipath);

     /*
      * When an iscsi iqn is present, it may have embedded colons, so
      * don't parse off anything.
      */
     if (device_kind != FILE_DEVICE_NET &&
	 device_kind != FILE_DEVICE_ISCSI &&
	 strchr(defdev, ':') != NULL) {
           if ((ptr = strrchr(defdev, ':')) != NULL)
                *ptr = 0; /* remove trailing : from defdevice if necessary */
     }

     /* This will not properly handle an obp-tftp argument list
      * with elements after the filename; that is handled below.
      */
     if (device_kind != FILE_DEVICE_NET &&
	 device_kind != FILE_DEVICE_ISCSI &&
	 strchr(ipath, ':') != NULL) {
	  if ((ptr = strrchr(ipath, ',')) != NULL) {
	       char *colon = strrchr(ipath, ':');
	       /* If a ':' occurs *after* a ',', then we assume that there is
		  no filename */
	       if (!colon || colon < ptr) {
		    result->file = strdup(ptr+1);
		    /* Trim the filename off */
		    *ptr = 0;
	       }
	  }
     }

     if (device_kind == FILE_DEVICE_NET) {
	  if (strchr(ipath, ':')) {
	       if (extract_netboot_args(ipath, result) == 0)
		   return 0;
	  } else {
               /* If we didn't get a ':' then look only in netinfo */
	       extract_netinfo_args(result);
	       result->file = strdup(ipath);
          }

	  if (!defdev)
	       result->dev = netdev_path_to_dev(ipath);
     } else if (device_kind != FILE_DEVICE_ISCSI &&
		(ptr = strrchr(ipath, ':')) != NULL) {
	  *ptr = 0;
	  result->dev = strdup(ipath);
	  if (*(ptr+1))
	       result->part = simple_strtol(ptr+1, NULL, 10);
     } else if (!defdev) {
	  result->dev = strdup(ipath);
     } else if (strlen(ipath)) {
          result->file = strdup(ipath);
     } else {
	  free(defdev);
	  return 0;
     }

     if (!result->dev && defdev)
	  result->dev = strdup(defdev);

     if (result->part < 0)
	  result->part = defpart;

     if (!result->file)
	  result->file = strdup(deffile);

     free(ipath);
     if (defdev)
          free(defdev);
     return 1;
}


static int
file_block_open(	struct boot_file_t*	file,
			struct boot_fspec_t*	fspec,
			int			partition)
{
     struct partition_t*	parts;
     struct partition_t*	p;
     struct partition_t*	found;

     parts = partitions_lookup(fspec->dev);
     found = NULL;

#if DEBUG
     if (parts)
	  prom_printf("partitions:\n");
     else
	  prom_printf("no partitions found.\n");
#endif
     for (p = parts; p && !found; p=p->next) {
	  DEBUG_F("number: %02d, start: 0x%08lx, length: 0x%08lx\n",
		  p->part_number, p->part_start, p->part_size );
	  if (partition == -1) {
	       file->fs = fs_open( file, p, fspec );
	       if (file->fs == NULL || fserrorno != FILE_ERR_OK)
		    continue;
	       else {
		    partition = p->part_number;
		    goto done;
	       }
	  }
	  if ((partition >= 0) && (partition == p->part_number))
	       found = p;
#if DEBUG
	  if (found)
	       prom_printf(" (match)\n");
#endif
     }

     /* Note: we don't skip when found is NULL since we can, in some
      * cases, let OF figure out a default partition.
      */
     DEBUG_F( "Using OF defaults.. (found = %p)\n", found );
     file->fs = fs_open( file, found, fspec );

done:
     if (parts)
	  partitions_free(parts);

     return fserrorno;
}

static int
file_net_open(struct boot_file_t* file, struct boot_fspec_t *fspec)
{
     file->fs = fs_of_netboot;
     return fs_of_netboot->open(file, NULL, fspec);
}

static int
default_read(	struct boot_file_t*	file,
		unsigned int		size,
		void*			buffer)
{
     prom_printf("WARNING ! default_read called !\n");
     return FILE_ERR_EOF;
}

static int
default_seek(	struct boot_file_t*	file,
		unsigned int		newpos)
{
     prom_printf("WARNING ! default_seek called !\n");
     return FILE_ERR_EOF;
}

static int
default_close(	struct boot_file_t*	file)
{
     prom_printf("WARNING ! default_close called !\n");
     return FILE_ERR_OK;
}

static struct fs_t fs_default =
{
     "defaults",
     NULL,
     default_read,
     default_seek,
     default_close
};


int open_file(struct boot_fspec_t* spec, struct boot_file_t* file)
{
     int result;

     memset(file, 0, sizeof(struct boot_file_t));
     file->fs        = &fs_default;

     DEBUG_F("dev_path = %s\nfile_name = %s\npartition = %d\n",
	     spec->dev, spec->file, spec->part);

     result = prom_get_devtype(spec->dev);
     if (result > 0)
	  file->device_kind = result;
     else
	  return result;

     switch(file->device_kind) {
     case FILE_DEVICE_BLOCK:
	  DEBUG_F("device is a block device\n");
	  return file_block_open(file, spec, spec->part);
     case FILE_DEVICE_ISCSI:
	  DEBUG_F("device is a iSCSI device\n");
	  return file_block_open(file, spec, spec->part);
     case FILE_DEVICE_NET:
	  DEBUG_F("device is a network device\n");
	  return file_net_open(file, spec);
     }
     return 0;
}

/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
