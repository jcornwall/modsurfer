#include "system.h"
#include "custom.h"
#include "gfx.h"
#include "ptplayer/ptplayer.h"

#include <clib/alib_protos.h>
#include <devices/input.h>
#include <devices/timer.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <exec/execbase.h>
#include <graphics/gfxbase.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#define kLibVerKick1 33
#define kLibVerKick3 39
#define kVBRLvl2IntOffset 0x68

// Defined in system.asm
extern void level2_int();
extern ULONG get_vbr();

static void allow_task_switch(BOOL allow);
static void set_intreq(UWORD intreq);

struct GfxBase* GfxBase;
struct IntuitionBase* IntuitionBase;

static struct {
  BOOL wb_closed;
  APTR save_windowptr;
  struct MsgPort* input_port;
  struct IOStdReq* input_io;
  BOOL input_opened;
  struct Interrupt* input_handler;
  BOOL input_handler_added;
  BOOL task_switch_disabled;
  BOOL blitter_owned;
  UWORD save_copcon;
  UWORD save_dmacon;
  struct View* save_view;
  UWORD save_intena;
  UWORD save_intreq;
  ULONG save_vbr_lvl2;
} g;

Status system_init() {
  Status status = StatusOK;

  ASSERT(GfxBase = (struct GfxBase*)OpenLibrary("graphics.library", kLibVerKick1));
  ASSERT(IntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library", kLibVerKick1));

  if (! system_is_rtg()) {
    g.wb_closed = CloseWorkBench();
  }

  // Suppress error requesters triggered by I/O.
  // We will control the display/input and user cannot respond to them.
  struct Process* process = (struct Process*)FindTask(NULL);
  g.save_windowptr = process->pr_WindowPtr;
  process->pr_WindowPtr = (APTR)-1;

cleanup:
  if (status != StatusOK) {
    system_fini();
  }

  return status;
}

void system_fini() {
  struct Process* process = (struct Process*)FindTask(NULL);
  process->pr_WindowPtr = g.save_windowptr;

  if (g.wb_closed) {
    OpenWorkBench();
    g.wb_closed = FALSE;
  }

  if (IntuitionBase) {
    CloseLibrary((struct Library*)IntuitionBase);
    IntuitionBase = NULL;
  }

  if (GfxBase) {
    CloseLibrary((struct Library*)GfxBase);
    GfxBase = NULL;
  }
}

void system_print_error(STRPTR msg) {
  // DOS needs task switching to handle Write request.
  // Console needs blitter to draw text.
  if (DOSBase && (! g.task_switch_disabled)) {
    // Let OS temporarily use blitter to draw into console.
    system_release_blitter();

    STRPTR out_strs[] = {"modsurfer: assert(", msg, ") failed\n"};
    BPTR out_handle = Output();

    for (UWORD i = 0; i < ARRAY_NELEMS(out_strs); ++ i) {
      Write(out_handle, out_strs[i], string_length(out_strs[i]));
    }

    system_acquire_blitter();
  }
}

Status system_time_micros(ULONG* time_micros) {
  Status status = StatusOK;
  struct MsgPort* port = NULL;
  struct timerequest* timer_io = NULL;
  BOOL timer_opened = FALSE;

  ASSERT(port = CreatePort(NULL, 0));
  ASSERT(timer_io = (struct timerequest*)CreateExtIO(port, sizeof(struct timerequest)));
  ASSERT(OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest*)timer_io, 0) == 0);
  timer_opened = TRUE;

  timer_io->tr_node.io_Command = TR_GETSYSTIME;
  DoIO((struct IORequest*)timer_io);

  // Only the microsecond component of the time is returned.
  // This is sufficient for seeding the random number generator.
  *time_micros = timer_io->tr_time.tv_micro;

cleanup:
  if (timer_opened) {
    CloseDevice((struct IORequest*)timer_io);
  }

  if (timer_io) {
    DeleteExtIO((struct IORequest*)timer_io);
  }

  if (port) {
    DeletePort(port);
  }

  return status;
}

Status system_add_input_handler(APTR handler_func,
                                APTR handler_data) {
  Status status = StatusOK;

  ASSERT(g.input_port = CreatePort(NULL, 0));
  ASSERT(g.input_io = (struct IOStdReq*)CreateExtIO(g.input_port, sizeof(struct IOStdReq)));
  ASSERT(OpenDevice("input.device", 0, (struct IORequest*)g.input_io, 0) == 0);
  g.input_opened = TRUE;

  ASSERT(g.input_handler = (struct Interrupt*)AllocMem(sizeof(struct Interrupt), MEMF_CLEAR));
  g.input_handler->is_Node.ln_Pri = 100;
  g.input_handler->is_Node.ln_Name = "ModSurfer";
  g.input_handler->is_Code = handler_func;
  g.input_handler->is_Data = handler_data;

  g.input_io->io_Data = (APTR)g.input_handler;
  g.input_io->io_Command = IND_ADDHANDLER;
  ASSERT(DoIO((struct IORequest*)g.input_io) == 0);
  g.input_handler_added = TRUE;

cleanup:
  return status;
}

void system_remove_input_handler() {
  if (g.input_handler_added) {
    g.input_io->io_Data = g.input_handler;
    g.input_io->io_Command = IND_REMHANDLER;
    g.input_handler_added = FALSE;
    DoIO((struct IORequest*)g.input_io);
  }

  if (g.input_handler) {
    FreeMem(g.input_handler, sizeof(struct Interrupt));
    g.input_handler = NULL;
  }

  if (g.input_opened) {
    CloseDevice((struct IORequest*)g.input_io);
    g.input_opened = FALSE;
  }

  if (g.input_io) {
    DeleteExtIO((struct IORequest*)g.input_io);
    g.input_io = NULL;
  }

  if (g.input_port) {
    DeletePort(g.input_port);
    g.input_port = NULL;
  }
}

void system_load_view(struct View* view) {
  g.save_view = GfxBase->ActiView;

  // Not sure if this helps RTG.
  LoadView(NULL);
  WaitTOF();
  WaitTOF();

  LoadView(view);
  WaitTOF();
  WaitTOF();
}

void system_unload_view() {
  if (g.save_view) {
    LoadView(g.save_view);
    g.save_view = NULL;
  }
}

Status system_list_drives(dirlist_t* drives) {
  Status status = StatusOK;

  dirlist_init(drives);

  // OS may change the following data structures, disable task switching.
  allow_task_switch(FALSE);

  struct DosInfo* dos_info = BADDR(DOSBase->dl_Root->rn_Info);
  struct DeviceNode* dev_list = BADDR(dos_info->di_DevInfo);

  for (struct DeviceNode* node = dev_list; node; node = BADDR(node->dn_Next)) {
    if (node->dn_Type == DLT_DEVICE && node->dn_Task) {
      ASSERT(dirlist_append(drives, EntryDir, BADDR(node->dn_Name) + 1));
    }
  }

  ASSERT(dirlist_sort(drives));

cleanup:
  allow_task_switch(TRUE);

  if (status != StatusOK) {
    dirlist_free(drives);
  }

  return status;
}

Status system_list_path(STRPTR path,
                        dirlist_t* entries) {
  Status status = StatusOK;
  BPTR lock = 0;
  UBYTE mod_prefix[] = {'M', 'O', 'D', '.'};
  UBYTE mod_suffix[] = {'.', 'M', 'O', 'D'};

  CHECK(string_length(path) > 0, StatusInvalidPath);

  // First entry is a link to the parent directory.
  dirlist_init(entries);
  ASSERT(dirlist_append(entries, EntryDir, "/"));

  lock = Lock(path, ACCESS_READ);
  CHECK(lock, StatusInvalidPath);

  static struct FileInfoBlock fib;
  CHECK(Examine(lock, &fib), StatusInvalidPath);

  while (ExNext(lock, &fib)) {
    // Use uppercase filenames for display and sorting.
    string_to_upper(fib.fib_FileName);

    dirlist_entry_type_t type;

    if (fib.fib_DirEntryType > 0) {
      type = EntryDir;
    }
    else {
      if (string_has_suffix(fib.fib_FileName, mod_suffix, sizeof(mod_suffix)) ||
          string_has_prefix(fib.fib_FileName, mod_prefix, sizeof(mod_prefix))) {
        type = EntryMod;
      }
      else {
        type = EntryFile;
      }
    }

    ASSERT(dirlist_append(entries, type, fib.fib_FileName));
  }

  ASSERT(dirlist_sort(entries));

cleanup:
  if (lock) {
    UnLock(lock);
  }

  if (status != StatusOK) {
    dirlist_free(entries);
  }

  return status;
}

void system_acquire_control() {
  // Disable task switching until control is released.
  allow_task_switch(FALSE);

  // Wait for any in-flight blits to complete.
  // Our new copperlist expects exclusive access to the blitter.
  gfx_wait_blit();

  // Save and enable copper access to blitter registers.
  g.save_copcon = custom.copcon;
  custom.copcon = COPCON_CDANG;

  // Save and enable DMA channels.
  g.save_dmacon = custom.dmaconr;
  custom.dmacon = DMACON_SET | DMACON_DMAEN | DMACON_BPLEN | DMACON_COPEN | DMACON_BLTEN | DMACON_SPREN;

  // Save and clear interrupt state.
  g.save_intena = custom.intenar;
  custom.intena = INTENA_CLEARALL;

  g.save_intreq = custom.intreqr;
  set_intreq(INTREQ_CLEARALL);

  // Clear keyboard state before installing interrupt handler.
  memory_clear((APTR)keyboard_state, sizeof(keyboard_state));

  // Save and replace level 2 interrupt handler.
  ULONG vbr = get_vbr();
  volatile ULONG* vbr_lvl2 = (volatile ULONG*)(vbr + kVBRLvl2IntOffset);

  g.save_vbr_lvl2 = *vbr_lvl2;
  *vbr_lvl2 = (ULONG)level2_int;

  // Enable PORTS interrupts for level 2 handler.
  custom.intena = INTENA_SET | INTENA_PORTS;

  // Install ptplayer interrupt handlers.
  mt_install_cia(&custom, (APTR)vbr, 1);
}

void system_release_control() {
  // Remove ptplayer interrupt handlers.
  mt_remove_cia(&custom);

  // Disable PORTS interrupts.
  custom.intena = INTENA_PORTS;

  // Restore level 2 interrupt handler.
  ULONG vbr = get_vbr();
  volatile ULONG* vbr_lvl2 = (volatile ULONG*)(vbr + kVBRLvl2IntOffset);

  *vbr_lvl2 = g.save_vbr_lvl2;

  // Restore interrupt state.
  set_intreq(INTREQ_CLEARALL);
  set_intreq(INTREQ_SET | g.save_intreq);

  custom.intena = INTENA_CLEARALL;
  custom.intena = INTENA_SET | g.save_intena;

  // Disable any extra DMA channels we enabled.
  custom.dmacon = g.save_dmacon ^ custom.dmaconr;

  // Wait until copper-initiated blits have finished.
  gfx_wait_vblank();
  gfx_wait_blit();

  // Restore copper access to blitter registers.
  custom.copcon = g.save_copcon;

  // Restore primary copperlist pointer.
  custom.cop1lc = (ULONG)GfxBase->copinit;

  // Enable task switching after control duration.
  allow_task_switch(TRUE);
}

static void allow_task_switch(BOOL allow) {
  if (allow && g.task_switch_disabled) {
    Permit();
    g.task_switch_disabled = FALSE;
  }
  else if ((! allow) && (! g.task_switch_disabled)) {
    Forbid();
    g.task_switch_disabled = TRUE;
  }
}

static void set_intreq(UWORD intreq) {
  // Repeat twice to work around A4000 040/060 bug.
  for (UWORD i = 0; i < 2; ++ i) {
    custom.intreq = intreq;
  }
}

void system_acquire_blitter() {
  if (! g.blitter_owned) {
    g.blitter_owned = TRUE;
    OwnBlitter();
  }
}

void system_release_blitter() {
  if (g.blitter_owned) {
    g.blitter_owned = FALSE;
    DisownBlitter();
  }
}

BOOL system_is_rtg() {
  BOOL is_rtg = FALSE;

  if (GfxBase->LibNode.lib_Version >= kLibVerKick3) {
    struct Screen* wb_screen = LockPubScreen("Workbench");

    if (wb_screen) {
      ULONG bitmap_attrs = GetBitMapAttr(&wb_screen->BitMap, BMA_FLAGS);
      is_rtg = (bitmap_attrs & BMF_STANDARD) == 0;

      UnlockPubScreen(NULL, wb_screen);
    }
  }

  return is_rtg;
}
