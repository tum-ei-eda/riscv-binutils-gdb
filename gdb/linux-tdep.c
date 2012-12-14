/* Target-dependent code for GNU/Linux, architecture independent.

   Copyright (C) 2009-2012 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "gdbtypes.h"
#include "linux-tdep.h"
#include "auxv.h"
#include "target.h"
#include "gdbthread.h"
#include "gdbcore.h"
#include "regcache.h"
#include "regset.h"
#include "elf/common.h"
#include "elf-bfd.h"            /* for elfcore_write_* */
#include "inferior.h"
#include "cli/cli-utils.h"
#include "arch-utils.h"
#include "gdb_obstack.h"

#include <ctype.h>

static struct gdbarch_data *linux_gdbarch_data_handle;

struct linux_gdbarch_data
  {
    struct type *siginfo_type;
  };

static void *
init_linux_gdbarch_data (struct gdbarch *gdbarch)
{
  return GDBARCH_OBSTACK_ZALLOC (gdbarch, struct linux_gdbarch_data);
}

static struct linux_gdbarch_data *
get_linux_gdbarch_data (struct gdbarch *gdbarch)
{
  return gdbarch_data (gdbarch, linux_gdbarch_data_handle);
}

/* This function is suitable for architectures that don't
   extend/override the standard siginfo structure.  */

struct type *
linux_get_siginfo_type (struct gdbarch *gdbarch)
{
  struct linux_gdbarch_data *linux_gdbarch_data;
  struct type *int_type, *uint_type, *long_type, *void_ptr_type;
  struct type *uid_type, *pid_type;
  struct type *sigval_type, *clock_type;
  struct type *siginfo_type, *sifields_type;
  struct type *type;

  linux_gdbarch_data = get_linux_gdbarch_data (gdbarch);
  if (linux_gdbarch_data->siginfo_type != NULL)
    return linux_gdbarch_data->siginfo_type;

  int_type = arch_integer_type (gdbarch, gdbarch_int_bit (gdbarch),
			 	0, "int");
  uint_type = arch_integer_type (gdbarch, gdbarch_int_bit (gdbarch),
				 1, "unsigned int");
  long_type = arch_integer_type (gdbarch, gdbarch_long_bit (gdbarch),
				 0, "long");
  void_ptr_type = lookup_pointer_type (builtin_type (gdbarch)->builtin_void);

  /* sival_t */
  sigval_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_UNION);
  TYPE_NAME (sigval_type) = xstrdup ("sigval_t");
  append_composite_type_field (sigval_type, "sival_int", int_type);
  append_composite_type_field (sigval_type, "sival_ptr", void_ptr_type);

  /* __pid_t */
  pid_type = arch_type (gdbarch, TYPE_CODE_TYPEDEF,
			TYPE_LENGTH (int_type), "__pid_t");
  TYPE_TARGET_TYPE (pid_type) = int_type;
  TYPE_TARGET_STUB (pid_type) = 1;

  /* __uid_t */
  uid_type = arch_type (gdbarch, TYPE_CODE_TYPEDEF,
			TYPE_LENGTH (uint_type), "__uid_t");
  TYPE_TARGET_TYPE (uid_type) = uint_type;
  TYPE_TARGET_STUB (uid_type) = 1;

  /* __clock_t */
  clock_type = arch_type (gdbarch, TYPE_CODE_TYPEDEF,
			  TYPE_LENGTH (long_type), "__clock_t");
  TYPE_TARGET_TYPE (clock_type) = long_type;
  TYPE_TARGET_STUB (clock_type) = 1;

  /* _sifields */
  sifields_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_UNION);

  {
    const int si_max_size = 128;
    int si_pad_size;
    int size_of_int = gdbarch_int_bit (gdbarch) / HOST_CHAR_BIT;

    /* _pad */
    if (gdbarch_ptr_bit (gdbarch) == 64)
      si_pad_size = (si_max_size / size_of_int) - 4;
    else
      si_pad_size = (si_max_size / size_of_int) - 3;
    append_composite_type_field (sifields_type, "_pad",
				 init_vector_type (int_type, si_pad_size));
  }

  /* _kill */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "si_pid", pid_type);
  append_composite_type_field (type, "si_uid", uid_type);
  append_composite_type_field (sifields_type, "_kill", type);

  /* _timer */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "si_tid", int_type);
  append_composite_type_field (type, "si_overrun", int_type);
  append_composite_type_field (type, "si_sigval", sigval_type);
  append_composite_type_field (sifields_type, "_timer", type);

  /* _rt */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "si_pid", pid_type);
  append_composite_type_field (type, "si_uid", uid_type);
  append_composite_type_field (type, "si_sigval", sigval_type);
  append_composite_type_field (sifields_type, "_rt", type);

  /* _sigchld */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "si_pid", pid_type);
  append_composite_type_field (type, "si_uid", uid_type);
  append_composite_type_field (type, "si_status", int_type);
  append_composite_type_field (type, "si_utime", clock_type);
  append_composite_type_field (type, "si_stime", clock_type);
  append_composite_type_field (sifields_type, "_sigchld", type);

  /* _sigfault */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "si_addr", void_ptr_type);
  append_composite_type_field (sifields_type, "_sigfault", type);

  /* _sigpoll */
  type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  append_composite_type_field (type, "si_band", long_type);
  append_composite_type_field (type, "si_fd", int_type);
  append_composite_type_field (sifields_type, "_sigpoll", type);

  /* struct siginfo */
  siginfo_type = arch_composite_type (gdbarch, NULL, TYPE_CODE_STRUCT);
  TYPE_NAME (siginfo_type) = xstrdup ("siginfo");
  append_composite_type_field (siginfo_type, "si_signo", int_type);
  append_composite_type_field (siginfo_type, "si_errno", int_type);
  append_composite_type_field (siginfo_type, "si_code", int_type);
  append_composite_type_field_aligned (siginfo_type,
				       "_sifields", sifields_type,
				       TYPE_LENGTH (long_type));

  linux_gdbarch_data->siginfo_type = siginfo_type;

  return siginfo_type;
}

static int
linux_has_shared_address_space (struct gdbarch *gdbarch)
{
  /* Determine whether we are running on uClinux or normal Linux
     kernel.  */
  CORE_ADDR dummy;
  int target_is_uclinux;

  target_is_uclinux
    = (target_auxv_search (&current_target, AT_NULL, &dummy) > 0
       && target_auxv_search (&current_target, AT_PAGESZ, &dummy) == 0);

  return target_is_uclinux;
}

/* This is how we want PTIDs from core files to be printed.  */

static char *
linux_core_pid_to_str (struct gdbarch *gdbarch, ptid_t ptid)
{
  static char buf[80];

  if (ptid_get_lwp (ptid) != 0)
    {
      snprintf (buf, sizeof (buf), "LWP %ld", ptid_get_lwp (ptid));
      return buf;
    }

  return normal_pid_to_str (ptid);
}

/* Service function for corefiles and info proc.  */

static void
read_mapping (const char *line,
	      ULONGEST *addr, ULONGEST *endaddr,
	      const char **permissions, size_t *permissions_len,
	      ULONGEST *offset,
              const char **device, size_t *device_len,
	      ULONGEST *inode,
	      const char **filename)
{
  const char *p = line;

  *addr = strtoulst (p, &p, 16);
  if (*p == '-')
    p++;
  *endaddr = strtoulst (p, &p, 16);

  while (*p && isspace (*p))
    p++;
  *permissions = p;
  while (*p && !isspace (*p))
    p++;
  *permissions_len = p - *permissions;

  *offset = strtoulst (p, &p, 16);

  while (*p && isspace (*p))
    p++;
  *device = p;
  while (*p && !isspace (*p))
    p++;
  *device_len = p - *device;

  *inode = strtoulst (p, &p, 10);

  while (*p && isspace (*p))
    p++;
  *filename = p;
}

/* Implement the "info proc" command.  */

static void
linux_info_proc (struct gdbarch *gdbarch, char *args,
		 enum info_proc_what what)
{
  /* A long is used for pid instead of an int to avoid a loss of precision
     compiler warning from the output of strtoul.  */
  long pid;
  int cmdline_f = (what == IP_MINIMAL || what == IP_CMDLINE || what == IP_ALL);
  int cwd_f = (what == IP_MINIMAL || what == IP_CWD || what == IP_ALL);
  int exe_f = (what == IP_MINIMAL || what == IP_EXE || what == IP_ALL);
  int mappings_f = (what == IP_MAPPINGS || what == IP_ALL);
  int status_f = (what == IP_STATUS || what == IP_ALL);
  int stat_f = (what == IP_STAT || what == IP_ALL);
  char filename[100];
  gdb_byte *data;
  int target_errno;

  if (args && isdigit (args[0]))
    pid = strtoul (args, &args, 10);
  else
    {
      if (!target_has_execution)
	error (_("No current process: you must name one."));
      if (current_inferior ()->fake_pid_p)
	error (_("Can't determine the current process's PID: you must name one."));

      pid = current_inferior ()->pid;
    }

  args = skip_spaces (args);
  if (args && args[0])
    error (_("Too many parameters: %s"), args);

  printf_filtered (_("process %ld\n"), pid);
  if (cmdline_f)
    {
      xsnprintf (filename, sizeof filename, "/proc/%ld/cmdline", pid);
      data = target_fileio_read_stralloc (filename);
      if (data)
	{
	  struct cleanup *cleanup = make_cleanup (xfree, data);
          printf_filtered ("cmdline = '%s'\n", data);
	  do_cleanups (cleanup);
	}
      else
	warning (_("unable to open /proc file '%s'"), filename);
    }
  if (cwd_f)
    {
      xsnprintf (filename, sizeof filename, "/proc/%ld/cwd", pid);
      data = target_fileio_readlink (filename, &target_errno);
      if (data)
	{
	  struct cleanup *cleanup = make_cleanup (xfree, data);
          printf_filtered ("cwd = '%s'\n", data);
	  do_cleanups (cleanup);
	}
      else
	warning (_("unable to read link '%s'"), filename);
    }
  if (exe_f)
    {
      xsnprintf (filename, sizeof filename, "/proc/%ld/exe", pid);
      data = target_fileio_readlink (filename, &target_errno);
      if (data)
	{
	  struct cleanup *cleanup = make_cleanup (xfree, data);
          printf_filtered ("exe = '%s'\n", data);
	  do_cleanups (cleanup);
	}
      else
	warning (_("unable to read link '%s'"), filename);
    }
  if (mappings_f)
    {
      xsnprintf (filename, sizeof filename, "/proc/%ld/maps", pid);
      data = target_fileio_read_stralloc (filename);
      if (data)
	{
	  struct cleanup *cleanup = make_cleanup (xfree, data);
	  char *line;

	  printf_filtered (_("Mapped address spaces:\n\n"));
	  if (gdbarch_addr_bit (gdbarch) == 32)
	    {
	      printf_filtered ("\t%10s %10s %10s %10s %s\n",
			   "Start Addr",
			   "  End Addr",
			   "      Size", "    Offset", "objfile");
            }
	  else
            {
	      printf_filtered ("  %18s %18s %10s %10s %s\n",
			   "Start Addr",
			   "  End Addr",
			   "      Size", "    Offset", "objfile");
	    }

	  for (line = strtok (data, "\n"); line; line = strtok (NULL, "\n"))
	    {
	      ULONGEST addr, endaddr, offset, inode;
	      const char *permissions, *device, *filename;
	      size_t permissions_len, device_len;

	      read_mapping (line, &addr, &endaddr,
			    &permissions, &permissions_len,
			    &offset, &device, &device_len,
			    &inode, &filename);

	      if (gdbarch_addr_bit (gdbarch) == 32)
	        {
	          printf_filtered ("\t%10s %10s %10s %10s %s\n",
				   paddress (gdbarch, addr),
				   paddress (gdbarch, endaddr),
				   hex_string (endaddr - addr),
				   hex_string (offset),
				   *filename? filename : "");
		}
	      else
	        {
	          printf_filtered ("  %18s %18s %10s %10s %s\n",
				   paddress (gdbarch, addr),
				   paddress (gdbarch, endaddr),
				   hex_string (endaddr - addr),
				   hex_string (offset),
				   *filename? filename : "");
	        }
	    }

	  do_cleanups (cleanup);
	}
      else
	warning (_("unable to open /proc file '%s'"), filename);
    }
  if (status_f)
    {
      xsnprintf (filename, sizeof filename, "/proc/%ld/status", pid);
      data = target_fileio_read_stralloc (filename);
      if (data)
	{
	  struct cleanup *cleanup = make_cleanup (xfree, data);
          puts_filtered (data);
	  do_cleanups (cleanup);
	}
      else
	warning (_("unable to open /proc file '%s'"), filename);
    }
  if (stat_f)
    {
      xsnprintf (filename, sizeof filename, "/proc/%ld/stat", pid);
      data = target_fileio_read_stralloc (filename);
      if (data)
	{
	  struct cleanup *cleanup = make_cleanup (xfree, data);
	  const char *p = data;

	  printf_filtered (_("Process: %s\n"),
			   pulongest (strtoulst (p, &p, 10)));

	  while (*p && isspace (*p))
	    p++;
	  if (*p == '(')
	    {
	      const char *ep = strchr (p, ')');
	      if (ep != NULL)
		{
		  printf_filtered ("Exec file: %.*s\n",
				   (int) (ep - p - 1), p + 1);
		  p = ep + 1;
		}
	    }

	  while (*p && isspace (*p))
	    p++;
	  if (*p)
	    printf_filtered (_("State: %c\n"), *p++);

	  if (*p)
	    printf_filtered (_("Parent process: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Process group: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Session id: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("TTY: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("TTY owner process group: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));

	  if (*p)
	    printf_filtered (_("Flags: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Minor faults (no memory page): %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Minor faults, children: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Major faults (memory page faults): %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Major faults, children: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("utime: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("stime: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("utime, children: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("stime, children: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("jiffies remaining in current "
			       "time slice: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("'nice' value: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("jiffies until next timeout: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("jiffies until next SIGALRM: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("start time (jiffies since "
			       "system boot): %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Virtual memory size: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Resident set size: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("rlim: %s\n"),
			     pulongest (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Start of text: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("End of text: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Start of stack: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
#if 0	/* Don't know how architecture-dependent the rest is...
	   Anyway the signal bitmap info is available from "status".  */
	  if (*p)
	    printf_filtered (_("Kernel stack pointer: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Kernel instr pointer: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Pending signals bitmap: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Blocked signals bitmap: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Ignored signals bitmap: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("Catched signals bitmap: %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
	  if (*p)
	    printf_filtered (_("wchan (system call): %s\n"),
			     hex_string (strtoulst (p, &p, 10)));
#endif
	  do_cleanups (cleanup);
	}
      else
	warning (_("unable to open /proc file '%s'"), filename);
    }
}

/* Implement "info proc mappings" for a corefile.  */

static void
linux_core_info_proc_mappings (struct gdbarch *gdbarch, char *args)
{
  asection *section;
  ULONGEST count, page_size;
  unsigned char *descdata, *filenames, *descend, *contents;
  size_t note_size;
  unsigned int addr_size_bits, addr_size;
  struct cleanup *cleanup;
  struct gdbarch *core_gdbarch = gdbarch_from_bfd (core_bfd);
  /* We assume this for reading 64-bit core files.  */
  gdb_static_assert (sizeof (ULONGEST) >= 8);

  section = bfd_get_section_by_name (core_bfd, ".note.linuxcore.file");
  if (section == NULL)
    {
      warning (_("unable to find mappings in core file"));
      return;
    }

  addr_size_bits = gdbarch_addr_bit (core_gdbarch);
  addr_size = addr_size_bits / 8;
  note_size = bfd_get_section_size (section);

  if (note_size < 2 * addr_size)
    error (_("malformed core note - too short for header"));

  contents = xmalloc (note_size);
  cleanup = make_cleanup (xfree, contents);
  if (!bfd_get_section_contents (core_bfd, section, contents, 0, note_size))
    error (_("could not get core note contents"));

  descdata = contents;
  descend = descdata + note_size;

  if (descdata[note_size - 1] != '\0')
    error (_("malformed note - does not end with \\0"));

  count = bfd_get (addr_size_bits, core_bfd, descdata);
  descdata += addr_size;

  page_size = bfd_get (addr_size_bits, core_bfd, descdata);
  descdata += addr_size;

  if (note_size < 2 * addr_size + count * 3 * addr_size)
    error (_("malformed note - too short for supplied file count"));

  printf_filtered (_("Mapped address spaces:\n\n"));
  if (gdbarch_addr_bit (gdbarch) == 32)
    {
      printf_filtered ("\t%10s %10s %10s %10s %s\n",
		       "Start Addr",
		       "  End Addr",
		       "      Size", "    Offset", "objfile");
    }
  else
    {
      printf_filtered ("  %18s %18s %10s %10s %s\n",
		       "Start Addr",
		       "  End Addr",
		       "      Size", "    Offset", "objfile");
    }

  filenames = descdata + count * 3 * addr_size;
  while (--count > 0)
    {
      ULONGEST start, end, file_ofs;

      if (filenames == descend)
	error (_("malformed note - filenames end too early"));

      start = bfd_get (addr_size_bits, core_bfd, descdata);
      descdata += addr_size;
      end = bfd_get (addr_size_bits, core_bfd, descdata);
      descdata += addr_size;
      file_ofs = bfd_get (addr_size_bits, core_bfd, descdata);
      descdata += addr_size;

      file_ofs *= page_size;

      if (gdbarch_addr_bit (gdbarch) == 32)
	printf_filtered ("\t%10s %10s %10s %10s %s\n",
			 paddress (gdbarch, start),
			 paddress (gdbarch, end),
			 hex_string (end - start),
			 hex_string (file_ofs),
			 filenames);
      else
	printf_filtered ("  %18s %18s %10s %10s %s\n",
			 paddress (gdbarch, start),
			 paddress (gdbarch, end),
			 hex_string (end - start),
			 hex_string (file_ofs),
			 filenames);

      filenames += 1 + strlen ((char *) filenames);
    }

  do_cleanups (cleanup);
}

/* Implement "info proc" for a corefile.  */

static void
linux_core_info_proc (struct gdbarch *gdbarch, char *args,
		      enum info_proc_what what)
{
  int exe_f = (what == IP_MINIMAL || what == IP_EXE || what == IP_ALL);
  int mappings_f = (what == IP_MAPPINGS || what == IP_ALL);

  if (exe_f)
    {
      const char *exe;

      exe = bfd_core_file_failing_command (core_bfd);
      if (exe != NULL)
	printf_filtered ("exe = '%s'\n", exe);
      else
	warning (_("unable to find command name in core file"));
    }

  if (mappings_f)
    linux_core_info_proc_mappings (gdbarch, args);

  if (!exe_f && !mappings_f)
    error (_("unable to handle request"));
}

typedef int linux_find_memory_region_ftype (ULONGEST vaddr, ULONGEST size,
					    ULONGEST offset, ULONGEST inode,
					    int read, int write,
					    int exec, int modified,
					    const char *filename,
					    void *data);

/* List memory regions in the inferior for a corefile.  */

static int
linux_find_memory_regions_full (struct gdbarch *gdbarch,
				linux_find_memory_region_ftype *func,
				void *obfd)
{
  char filename[100];
  gdb_byte *data;

  /* We need to know the real target PID to access /proc.  */
  if (current_inferior ()->fake_pid_p)
    return 1;

  xsnprintf (filename, sizeof filename,
	     "/proc/%d/smaps", current_inferior ()->pid);
  data = target_fileio_read_stralloc (filename);
  if (data == NULL)
    {
      /* Older Linux kernels did not support /proc/PID/smaps.  */
      xsnprintf (filename, sizeof filename,
		 "/proc/%d/maps", current_inferior ()->pid);
      data = target_fileio_read_stralloc (filename);
    }
  if (data)
    {
      struct cleanup *cleanup = make_cleanup (xfree, data);
      char *line;

      line = strtok (data, "\n");
      while (line)
	{
	  ULONGEST addr, endaddr, offset, inode;
	  const char *permissions, *device, *filename;
	  size_t permissions_len, device_len;
	  int read, write, exec;
	  int modified = 0, has_anonymous = 0;

	  read_mapping (line, &addr, &endaddr, &permissions, &permissions_len,
			&offset, &device, &device_len, &inode, &filename);

	  /* Decode permissions.  */
	  read = (memchr (permissions, 'r', permissions_len) != 0);
	  write = (memchr (permissions, 'w', permissions_len) != 0);
	  exec = (memchr (permissions, 'x', permissions_len) != 0);

	  /* Try to detect if region was modified by parsing smaps counters.  */
	  for (line = strtok (NULL, "\n");
	       line && line[0] >= 'A' && line[0] <= 'Z';
	       line = strtok (NULL, "\n"))
	    {
	      char keyword[64 + 1];
	      unsigned long number;

	      if (sscanf (line, "%64s%lu kB\n", keyword, &number) != 2)
		{
		  warning (_("Error parsing {s,}maps file '%s'"), filename);
		  break;
		}
	      if (strcmp (keyword, "Anonymous:") == 0)
		has_anonymous = 1;
	      if (number != 0 && (strcmp (keyword, "Shared_Dirty:") == 0
				  || strcmp (keyword, "Private_Dirty:") == 0
				  || strcmp (keyword, "Swap:") == 0
				  || strcmp (keyword, "Anonymous:") == 0))
		modified = 1;
	    }

	  /* Older Linux kernels did not support the "Anonymous:" counter.
	     If it is missing, we can't be sure - dump all the pages.  */
	  if (!has_anonymous)
	    modified = 1;

	  /* Invoke the callback function to create the corefile segment.  */
	  func (addr, endaddr - addr, offset, inode,
		read, write, exec, modified, filename, obfd);
	}

      do_cleanups (cleanup);
      return 0;
    }

  return 1;
}

/* A structure for passing information through
   linux_find_memory_regions_full.  */

struct linux_find_memory_regions_data
{
  /* The original callback.  */

  find_memory_region_ftype func;

  /* The original datum.  */

  void *obfd;
};

/* A callback for linux_find_memory_regions that converts between the
   "full"-style callback and find_memory_region_ftype.  */

static int
linux_find_memory_regions_thunk (ULONGEST vaddr, ULONGEST size,
				 ULONGEST offset, ULONGEST inode,
				 int read, int write, int exec, int modified,
				 const char *filename, void *arg)
{
  struct linux_find_memory_regions_data *data = arg;

  return data->func (vaddr, size, read, write, exec, modified, data->obfd);
}

/* A variant of linux_find_memory_regions_full that is suitable as the
   gdbarch find_memory_regions method.  */

static int
linux_find_memory_regions (struct gdbarch *gdbarch,
			   find_memory_region_ftype func, void *obfd)
{
  struct linux_find_memory_regions_data data;

  data.func = func;
  data.obfd = obfd;

  return linux_find_memory_regions_full (gdbarch,
					 linux_find_memory_regions_thunk,
					 &data);
}

/* Determine which signal stopped execution.  */

static int
find_signalled_thread (struct thread_info *info, void *data)
{
  if (info->suspend.stop_signal != GDB_SIGNAL_0
      && ptid_get_pid (info->ptid) == ptid_get_pid (inferior_ptid))
    return 1;

  return 0;
}

static enum gdb_signal
find_stop_signal (void)
{
  struct thread_info *info =
    iterate_over_threads (find_signalled_thread, NULL);

  if (info)
    return info->suspend.stop_signal;
  else
    return GDB_SIGNAL_0;
}

/* Generate corefile notes for SPU contexts.  */

static char *
linux_spu_make_corefile_notes (bfd *obfd, char *note_data, int *note_size)
{
  static const char *spu_files[] =
    {
      "object-id",
      "mem",
      "regs",
      "fpcr",
      "lslr",
      "decr",
      "decr_status",
      "signal1",
      "signal1_type",
      "signal2",
      "signal2_type",
      "event_mask",
      "event_status",
      "mbox_info",
      "ibox_info",
      "wbox_info",
      "dma_info",
      "proxydma_info",
   };

  enum bfd_endian byte_order = gdbarch_byte_order (target_gdbarch ());
  gdb_byte *spu_ids;
  LONGEST i, j, size;

  /* Determine list of SPU ids.  */
  size = target_read_alloc (&current_target, TARGET_OBJECT_SPU,
			    NULL, &spu_ids);

  /* Generate corefile notes for each SPU file.  */
  for (i = 0; i < size; i += 4)
    {
      int fd = extract_unsigned_integer (spu_ids + i, 4, byte_order);

      for (j = 0; j < sizeof (spu_files) / sizeof (spu_files[0]); j++)
	{
	  char annex[32], note_name[32];
	  gdb_byte *spu_data;
	  LONGEST spu_len;

	  xsnprintf (annex, sizeof annex, "%d/%s", fd, spu_files[j]);
	  spu_len = target_read_alloc (&current_target, TARGET_OBJECT_SPU,
				       annex, &spu_data);
	  if (spu_len > 0)
	    {
	      xsnprintf (note_name, sizeof note_name, "SPU/%s", annex);
	      note_data = elfcore_write_note (obfd, note_data, note_size,
					      note_name, NT_SPU,
					      spu_data, spu_len);
	      xfree (spu_data);

	      if (!note_data)
		{
		  xfree (spu_ids);
		  return NULL;
		}
	    }
	}
    }

  if (size > 0)
    xfree (spu_ids);

  return note_data;
}

/* This is used to pass information from
   linux_make_mappings_corefile_notes through
   linux_find_memory_regions_full.  */

struct linux_make_mappings_data
{
  /* Number of files mapped.  */
  ULONGEST file_count;

  /* The obstack for the main part of the data.  */
  struct obstack *data_obstack;

  /* The filename obstack.  */
  struct obstack *filename_obstack;

  /* The architecture's "long" type.  */
  struct type *long_type;
};

static linux_find_memory_region_ftype linux_make_mappings_callback;

/* A callback for linux_find_memory_regions_full that updates the
   mappings data for linux_make_mappings_corefile_notes.  */

static int
linux_make_mappings_callback (ULONGEST vaddr, ULONGEST size,
			      ULONGEST offset, ULONGEST inode,
			      int read, int write, int exec, int modified,
			      const char *filename, void *data)
{
  struct linux_make_mappings_data *map_data = data;
  gdb_byte buf[sizeof (ULONGEST)];

  if (*filename == '\0' || inode == 0)
    return 0;

  ++map_data->file_count;

  pack_long (buf, map_data->long_type, vaddr);
  obstack_grow (map_data->data_obstack, buf, TYPE_LENGTH (map_data->long_type));
  pack_long (buf, map_data->long_type, vaddr + size);
  obstack_grow (map_data->data_obstack, buf, TYPE_LENGTH (map_data->long_type));
  pack_long (buf, map_data->long_type, offset);
  obstack_grow (map_data->data_obstack, buf, TYPE_LENGTH (map_data->long_type));

  obstack_grow_str0 (map_data->filename_obstack, filename);

  return 0;
}

/* Write the file mapping data to the core file, if possible.  OBFD is
   the output BFD.  NOTE_DATA is the current note data, and NOTE_SIZE
   is a pointer to the note size.  Returns the new NOTE_DATA and
   updates NOTE_SIZE.  */

static char *
linux_make_mappings_corefile_notes (struct gdbarch *gdbarch, bfd *obfd,
				    char *note_data, int *note_size)
{
  struct cleanup *cleanup;
  struct obstack data_obstack, filename_obstack;
  struct linux_make_mappings_data mapping_data;
  struct type *long_type
    = arch_integer_type (gdbarch, gdbarch_long_bit (gdbarch), 0, "long");
  gdb_byte buf[sizeof (ULONGEST)];

  obstack_init (&data_obstack);
  cleanup = make_cleanup_obstack_free (&data_obstack);
  obstack_init (&filename_obstack);
  make_cleanup_obstack_free (&filename_obstack);

  mapping_data.file_count = 0;
  mapping_data.data_obstack = &data_obstack;
  mapping_data.filename_obstack = &filename_obstack;
  mapping_data.long_type = long_type;

  /* Reserve space for the count.  */
  obstack_blank (&data_obstack, TYPE_LENGTH (long_type));
  /* We always write the page size as 1 since we have no good way to
     determine the correct value.  */
  pack_long (buf, long_type, 1);
  obstack_grow (&data_obstack, buf, TYPE_LENGTH (long_type));

  linux_find_memory_regions_full (gdbarch, linux_make_mappings_callback,
				  &mapping_data);

  if (mapping_data.file_count != 0)
    {
      /* Write the count to the obstack.  */
      pack_long (obstack_base (&data_obstack), long_type,
		 mapping_data.file_count);

      /* Copy the filenames to the data obstack.  */
      obstack_grow (&data_obstack, obstack_base (&filename_obstack),
		    obstack_object_size (&filename_obstack));

      note_data = elfcore_write_note (obfd, note_data, note_size,
				      "CORE", NT_FILE,
				      obstack_base (&data_obstack),
				      obstack_object_size (&data_obstack));
    }

  do_cleanups (cleanup);
  return note_data;
}

/* Records the thread's register state for the corefile note
   section.  */

static char *
linux_collect_thread_registers (const struct regcache *regcache,
				ptid_t ptid, bfd *obfd,
				char *note_data, int *note_size,
				enum gdb_signal stop_signal)
{
  struct gdbarch *gdbarch = get_regcache_arch (regcache);
  struct core_regset_section *sect_list;
  unsigned long lwp;

  sect_list = gdbarch_core_regset_sections (gdbarch);
  gdb_assert (sect_list);

  /* For remote targets the LWP may not be available, so use the TID.  */
  lwp = ptid_get_lwp (ptid);
  if (!lwp)
    lwp = ptid_get_tid (ptid);

  while (sect_list->sect_name != NULL)
    {
      const struct regset *regset;
      char *buf;

      regset = gdbarch_regset_from_core_section (gdbarch,
						 sect_list->sect_name,
						 sect_list->size);
      gdb_assert (regset && regset->collect_regset);

      buf = xmalloc (sect_list->size);
      regset->collect_regset (regset, regcache, -1, buf, sect_list->size);

      /* PRSTATUS still needs to be treated specially.  */
      if (strcmp (sect_list->sect_name, ".reg") == 0)
	note_data = (char *) elfcore_write_prstatus
			       (obfd, note_data, note_size, lwp,
				gdb_signal_to_host (stop_signal), buf);
      else
	note_data = (char *) elfcore_write_register_note
			       (obfd, note_data, note_size,
				sect_list->sect_name, buf, sect_list->size);
      xfree (buf);
      sect_list++;

      if (!note_data)
	return NULL;
    }

  return note_data;
}

/* Fetch the siginfo data for the current thread, if it exists.  If
   there is no data, or we could not read it, return NULL.  Otherwise,
   return a newly malloc'd buffer holding the data and fill in *SIZE
   with the size of the data.  The caller is responsible for freeing
   the data.  */

static gdb_byte *
linux_get_siginfo_data (struct gdbarch *gdbarch, LONGEST *size)
{
  struct type *siginfo_type;
  gdb_byte *buf;
  LONGEST bytes_read;
  struct cleanup *cleanups;

  if (!gdbarch_get_siginfo_type_p (gdbarch))
    return NULL;
  
  siginfo_type = gdbarch_get_siginfo_type (gdbarch);

  buf = xmalloc (TYPE_LENGTH (siginfo_type));
  cleanups = make_cleanup (xfree, buf);

  bytes_read = target_read (&current_target, TARGET_OBJECT_SIGNAL_INFO, NULL,
			    buf, 0, TYPE_LENGTH (siginfo_type));
  if (bytes_read == TYPE_LENGTH (siginfo_type))
    {
      discard_cleanups (cleanups);
      *size = bytes_read;
    }
  else
    {
      do_cleanups (cleanups);
      buf = NULL;
    }

  return buf;
}

struct linux_corefile_thread_data
{
  struct gdbarch *gdbarch;
  int pid;
  bfd *obfd;
  char *note_data;
  int *note_size;
  int num_notes;
  enum gdb_signal stop_signal;
  linux_collect_thread_registers_ftype collect;
};

/* Called by gdbthread.c once per thread.  Records the thread's
   register state for the corefile note section.  */

static int
linux_corefile_thread_callback (struct thread_info *info, void *data)
{
  struct linux_corefile_thread_data *args = data;

  if (ptid_get_pid (info->ptid) == args->pid)
    {
      struct cleanup *old_chain;
      struct regcache *regcache;
      gdb_byte *siginfo_data;
      LONGEST siginfo_size;

      regcache = get_thread_arch_regcache (info->ptid, args->gdbarch);

      old_chain = save_inferior_ptid ();
      inferior_ptid = info->ptid;
      target_fetch_registers (regcache, -1);
      siginfo_data = linux_get_siginfo_data (args->gdbarch, &siginfo_size);
      do_cleanups (old_chain);

      old_chain = make_cleanup (xfree, siginfo_data);

      args->note_data = args->collect (regcache, info->ptid, args->obfd,
				       args->note_data, args->note_size,
				       args->stop_signal);
      args->num_notes++;

      if (siginfo_data != NULL)
	{
	  args->note_data = elfcore_write_note (args->obfd,
						args->note_data,
						args->note_size,
						"CORE", NT_SIGINFO,
						siginfo_data, siginfo_size);
	  args->num_notes++;
	}

      do_cleanups (old_chain);
    }

  return !args->note_data;
}

/* Fills the "to_make_corefile_note" target vector.  Builds the note
   section for a corefile, and returns it in a malloc buffer.  */

char *
linux_make_corefile_notes (struct gdbarch *gdbarch, bfd *obfd, int *note_size,
			   linux_collect_thread_registers_ftype collect)
{
  struct linux_corefile_thread_data thread_args;
  char *note_data = NULL;
  gdb_byte *auxv;
  int auxv_len;

  /* Process information.  */
  if (get_exec_file (0))
    {
      const char *fname = lbasename (get_exec_file (0));
      char *psargs = xstrdup (fname);

      if (get_inferior_args ())
        psargs = reconcat (psargs, psargs, " ", get_inferior_args (),
			   (char *) NULL);

      note_data = elfcore_write_prpsinfo (obfd, note_data, note_size,
                                          fname, psargs);
      xfree (psargs);

      if (!note_data)
	return NULL;
    }

  /* Thread register information.  */
  thread_args.gdbarch = gdbarch;
  thread_args.pid = ptid_get_pid (inferior_ptid);
  thread_args.obfd = obfd;
  thread_args.note_data = note_data;
  thread_args.note_size = note_size;
  thread_args.num_notes = 0;
  thread_args.stop_signal = find_stop_signal ();
  thread_args.collect = collect;
  iterate_over_threads (linux_corefile_thread_callback, &thread_args);
  note_data = thread_args.note_data;
  if (!note_data)
    return NULL;

  /* Auxillary vector.  */
  auxv_len = target_read_alloc (&current_target, TARGET_OBJECT_AUXV,
				NULL, &auxv);
  if (auxv_len > 0)
    {
      note_data = elfcore_write_note (obfd, note_data, note_size,
				      "CORE", NT_AUXV, auxv, auxv_len);
      xfree (auxv);

      if (!note_data)
	return NULL;
    }

  /* SPU information.  */
  note_data = linux_spu_make_corefile_notes (obfd, note_data, note_size);
  if (!note_data)
    return NULL;

  /* File mappings.  */
  note_data = linux_make_mappings_corefile_notes (gdbarch, obfd,
						  note_data, note_size);

  make_cleanup (xfree, note_data);
  return note_data;
}

static char *
linux_make_corefile_notes_1 (struct gdbarch *gdbarch, bfd *obfd, int *note_size)
{
  /* FIXME: uweigand/2011-10-06: Once all GNU/Linux architectures have been
     converted to gdbarch_core_regset_sections, we no longer need to fall back
     to the target method at this point.  */

  if (!gdbarch_core_regset_sections (gdbarch))
    return target_make_corefile_notes (obfd, note_size);
  else
    return linux_make_corefile_notes (gdbarch, obfd, note_size,
				      linux_collect_thread_registers);
}

/* To be called from the various GDB_OSABI_LINUX handlers for the
   various GNU/Linux architectures and machine types.  */

void
linux_init_abi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  set_gdbarch_core_pid_to_str (gdbarch, linux_core_pid_to_str);
  set_gdbarch_info_proc (gdbarch, linux_info_proc);
  set_gdbarch_core_info_proc (gdbarch, linux_core_info_proc);
  set_gdbarch_find_memory_regions (gdbarch, linux_find_memory_regions);
  set_gdbarch_make_corefile_notes (gdbarch, linux_make_corefile_notes_1);
  set_gdbarch_has_shared_address_space (gdbarch,
					linux_has_shared_address_space);
}

/* Provide a prototype to silence -Wmissing-prototypes.  */
extern initialize_file_ftype _initialize_linux_tdep;

void
_initialize_linux_tdep (void)
{
  linux_gdbarch_data_handle =
    gdbarch_data_register_post_init (init_linux_gdbarch_data);
}
