/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C)      2023  Benjamin David Lunt
//  Copyright (C) 2003-2024  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA

#include "bochs.h"
#include "siminterface.h"
#include "param_names.h"

#if BX_USB_DEBUGGER

#include "usb_debug.h"
// hacks for iodev / USB internals
#include "iodev.h"
#include "iodev/usb/usb_common.h"

#include <gtk/gtk.h>
#include <glib.h>

typedef struct {
  const char *name;
  GtkWidget *entry;
  Bit8u fmt_size;
  const struct S_ATTRIBUTES *attr;
} usb_reg_t;

const char *chkTxt[6] = {"Reset", "Enable", "Event", "Doorbell", "Start of Frame", "Non-Exist"};
const char *chkBXPN[6] = {BXPN_USB_DEBUG_RESET, BXPN_USB_DEBUG_ENABLE, BXPN_USB_DEBUG_EVENT,
                          BXPN_USB_DEBUG_DOORBELL, BXPN_USB_DEBUG_START_FRAME, BXPN_USB_DEBUG_NON_EXIST};

int usbdbg_param1, usbdbg_param2;

GtkWidget *main_dialog, *td_dialog;
GtkWidget *uhci_entry[UHCI_REG_COUNT];
GtkWidget *DFframe, *DFvbox, *checkbox[6];
GtkWidget *apply_button;

// multithreading using pure posix threads -- not glib threads
static void * EventLp(void *data)
{
  gtk_main();
  return NULL;
}

static void MakeGTKthreads()
{
  pthread_t hThread;
  pthread_create(&hThread, NULL, EventLp, NULL);
}

GtkWidget* usbdlg_create_label(GtkWidget *box, const char *text, bool expand = true)
{
  GtkWidget *label = gtk_label_new(text);
  gtk_box_pack_start(GTK_BOX(box), label, expand, expand, 2);
  return label;
}

GtkWidget* usbdlg_create_entry_with_label(GtkWidget **box, const char *text, 
                                          bool expand = true)
{
  gtk_box_pack_start(GTK_BOX(box[0]), gtk_label_new(text), expand, expand, 2);
  GtkWidget *entry = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(box[1]), entry, expand, expand, 2);
  return entry;
}

GtkWidget* usbdlg_create_ro_entry(GtkWidget *box, bool expand = true)
{
  GtkWidget *entry = gtk_entry_new();
  gtk_widget_set_sensitive(entry, 0);
  gtk_box_pack_start(GTK_BOX(box), entry, expand, expand, 2);
  return entry;
}

GtkWidget* usbdlg_create_ro_entry_with_label(GtkWidget **box, const char *text,
                                             bool expand = true)
{
  gtk_box_pack_start(GTK_BOX(box[0]), gtk_label_new(text), expand, expand, 2);
  GtkWidget *entry = gtk_entry_new();
  gtk_widget_set_sensitive(entry, 0);
  gtk_box_pack_start(GTK_BOX(box[1]), entry, expand, expand, 2);
  return entry;
}

void usbdlg_create_debug_flags(GtkWidget *vbox)
{
  DFframe = gtk_frame_new("Debug Flags");
  gtk_box_pack_start(GTK_BOX(vbox), DFframe, FALSE, FALSE, 2);
  DFvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(DFframe), DFvbox);
  for (int i = 0; i < 6; i++) {
    checkbox[i] = gtk_check_button_new_with_label(chkTxt[i]);
    if (SIM->get_param_num(chkBXPN[i])->get() > 0) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[i]), TRUE);
    }
    gtk_box_pack_start(GTK_BOX(DFvbox), checkbox[i], FALSE, FALSE, 2);
  }
}

void usbdlg_set_debug_flags()
{
  for (int i = 0; i < 6; i++) {
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[i]))) {
      SIM->get_param_num(chkBXPN[i])->set(1);
    } else {
      SIM->get_param_num(chkBXPN[i])->set(0);
    }
  }
}

static void usb_regview_dialog(GtkWidget *widget, gpointer data)
{
  int i, ret;
  char buffer[32], format[10];
  Bit32u oldval, value;

  usb_reg_t *usb_reg_def = (usb_reg_t*)data;
  GtkWidget *dialog =
    gtk_dialog_new_with_buttons(usb_reg_def->name, GTK_WINDOW(main_dialog), GTK_DIALOG_MODAL,
                                g_dgettext("gtk30", "_OK"), GTK_RESPONSE_OK,
                                g_dgettext("gtk30", "_Cancel"), GTK_RESPONSE_CANCEL,
                                NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 250, 250);
  GtkWidget *listbox = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(listbox), GTK_SELECTION_MULTIPLE);
  gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(listbox), 1);
  i = 0;
  while (usb_reg_def->attr[i].index > -1) {
    gtk_list_box_insert(GTK_LIST_BOX(listbox), gtk_label_new(usb_reg_def->attr[i].str), -1);
    i++;
  }
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), listbox, TRUE, TRUE, 2);
  // Show dialog
  gtk_widget_show_all(dialog);
  oldval = strtoul(gtk_entry_get_text(GTK_ENTRY(usb_reg_def->entry)), NULL, 16);
  i = 0;
  while (usb_reg_def->attr[i].index > -1) {
    if ((oldval & usb_reg_def->attr[i].mask) != 0) {
      gtk_list_box_select_row(GTK_LIST_BOX(listbox),
                              gtk_list_box_get_row_at_index(GTK_LIST_BOX(listbox), i));
    } else {
      gtk_list_box_unselect_row(GTK_LIST_BOX(listbox),
                                gtk_list_box_get_row_at_index(GTK_LIST_BOX(listbox), i));
    }
    i++;
  }
  ret = gtk_dialog_run(GTK_DIALOG(dialog));
  if (ret == GTK_RESPONSE_OK) {
    value = 0;
    i = 0;
    while (usb_reg_def->attr[i].index > -1) {
      if (gtk_list_box_row_is_selected(gtk_list_box_get_row_at_index(GTK_LIST_BOX(listbox), i))) {
        value |= usb_reg_def->attr[i].mask;
      }
      i++;
    }
    if (value != oldval) {
      sprintf(format, "0x%%0%iX", usb_reg_def->fmt_size);
      sprintf(buffer, format, value);
      gtk_entry_set_text(GTK_ENTRY(usb_reg_def->entry), buffer);
    }
  }
  gtk_widget_destroy(dialog);
}

// Save UHCI state to controller

int hc_uhci_save()
{
  char buffer[COMMON_STR_SIZE];

  if (u_changed[UHCI_REG_COMMAND]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_COMMAND])));
    usb_io_write(pci_bar_address + 0, strtol(buffer, NULL, 0), 2);
  }
  if (u_changed[UHCI_REG_STATUS]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_STATUS])));
    usb_io_write(pci_bar_address + 2, strtol(buffer, NULL, 0), 2);
  }
  if (u_changed[UHCI_REG_INTERRUPT]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_INTERRUPT])));
    usb_io_write(pci_bar_address + 4, strtol(buffer, NULL, 0), 2);
  }
  if (u_changed[UHCI_REG_FRAME_NUM]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_FRAME_NUM])));
    usb_io_write(pci_bar_address + 6, strtol(buffer, NULL, 0), 2);
  }
  if (u_changed[UHCI_REG_FRAME_ADDR]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_FRAME_ADDR])));
    usb_io_write(pci_bar_address + 8, strtol(buffer, NULL, 0), 4);
  }
  if (u_changed[UHCI_REG_SOF]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_SOF])));
    usb_io_write(pci_bar_address + 12, strtol(buffer, NULL, 0), 1);
  }
  if (u_changed[UHCI_REG_PORT0]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_PORT0])));
    usb_io_write(pci_bar_address + 16, strtol(buffer, NULL, 0), 2);
  }
  if (u_changed[UHCI_REG_PORT1]) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(uhci_entry[UHCI_REG_PORT1])));
    usb_io_write(pci_bar_address + 18, strtol(buffer, NULL, 0), 2);
  }

  memset(u_changed, 0, sizeof(u_changed));
  gtk_widget_set_sensitive(apply_button, 0);

  return 0;
}

// Apply button support

static void on_entry_changed(GtkWidget *widget, gpointer data)
{
  *((bool*)data) = 1;
  gtk_widget_set_sensitive(apply_button, 1);
}

static void apply_changes(GtkWidget *widget, gpointer data)
{
  if (usb_debug_type == USB_DEBUG_UHCI) {
    hc_uhci_save();
  } else {
    GtkWidget* error = gtk_message_dialog_new(
      GTK_WINDOW(main_dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
      "Saving changes to controller not implemented yet");
    gtk_window_set_title(GTK_WINDOW(error), "WARNING");
    gtk_dialog_run(GTK_DIALOG(error));
    gtk_widget_destroy(error);
  }
}

void usbdlg_create_apply_button(GtkWidget *vbox)
{
  GtkWidget *apply_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(vbox), apply_hbox, FALSE, FALSE, 2);
  apply_button = gtk_button_new_with_label(g_dgettext("gtk30", "_Apply"));
  gtk_widget_set_sensitive(apply_button, 0);
  g_signal_connect(apply_button, "clicked", G_CALLBACK(apply_changes), NULL);
  gtk_box_pack_start(GTK_BOX(apply_hbox), apply_button, FALSE, FALSE, 2);
}

// Tree view support
#define MAX_TREE_ITEMS 50
int tree_items;
GtkTreeIter titems[MAX_TREE_ITEMS];

GtkTreeIter* treeview_insert(GtkWidget *treeview, GtkTreeIter *parent, char *str, bool bold)
{
  GtkTreeStore *treestore;
  GtkTreeViewColumn *treecol;
  GtkCellRenderer *renderer;

  treestore = (GtkTreeStore*)gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));
  if (treestore == NULL) {
    treecol = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(treecol, str);
    gtk_tree_view_append_column(GTK_TREE_VIEW(treeview), treecol);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(treecol, renderer, TRUE);
    gtk_tree_view_column_add_attribute(treecol, renderer, "text", 0);   // pull display text from treestore col 0
    gtk_widget_set_can_focus(treeview, FALSE);
    treestore = gtk_tree_store_new(1, G_TYPE_STRING);
    gtk_tree_view_set_model(GTK_TREE_VIEW(treeview), GTK_TREE_MODEL(treestore));
    g_object_unref(treestore);
    tree_items = 0;
    return NULL;
  } else {
    gtk_tree_store_append(treestore, &titems[tree_items], parent);
    gtk_tree_store_set(treestore, &titems[tree_items], 0, str, -1);
    tree_items++;
    return &titems[tree_items - 1];
  }
}

void hc_uhci_do_item(GtkWidget *treeview, Bit32u FrameAddr, Bit32u FrameNum)
{
  struct USB_UHCI_QUEUE_STACK queue_stack;
  Bit32u item, queue_addr = 0;
  struct QUEUE queue;
  struct TD td;
  char str[COMMON_STR_SIZE];
  GtkTreeIter *cur, *next;
  GtkTreeSelection *selection;
  bool state;

  // get the frame pointer
  DEV_MEM_READ_PHYSICAL(FrameAddr, sizeof(Bit32u), (Bit8u *) &item);
  sprintf(str, "Frame Pointer(%i): 0x%08X", FrameNum, item);
  next = treeview_insert(treeview, NULL, str, 0);

  queue_stack.queue_cnt = 0;

  // A catch to make sure we don't do too many
  while (tree_items < MAX_TREE_ITEMS) {
    if (!USB_UHCI_IS_LINK_VALID(item))  // the the T bit is set, we are done
      break;

    // is it a queue?
    if (USB_UHCI_IS_LINK_QUEUE(item)) {
      // add it to our current list of queues
      if (uhci_add_queue(&queue_stack, item & ~0xF)) {
        // if this queue has been added before, stop.
        break;
      }

      // read in the queue
      DEV_MEM_READ_PHYSICAL(item & ~0xF, sizeof(struct QUEUE), (Bit8u *) &queue);
      sprintf(str, "0x%08X: Queue Head: (0x%08X 0x%08X)", item & ~0xF, queue.horz, queue.vert);
      next = treeview_insert(treeview, next, str, 0);

      // if the vert pointer is valid, there are td's in it to process
      //  else only the head pointer may be valid
      if (!USB_UHCI_IS_LINK_VALID(queue.vert)) {
        // no vertical elements to process
        // (clear queue_addr to indicate we are not processing
        //  elements of the vertical part of a queue)
        queue_addr = 0;
        item = queue.horz;
      } else {
        // there are vertical elements to process
        // (save the address of the horz pointer in queue_addr
        //  so that we may update the queue's vertical pointer
        //  member with the successfully processed TD's link pointer)
        queue_addr = item;
        item = queue.vert;
      }
      continue;
    }

    // we processed another td within this queue line
    state = false; // clear the state
    DEV_MEM_READ_PHYSICAL(item & ~0xF, sizeof(struct TD), (Bit8u *) &td);
    const bool depthbreadth = (td.dword0 & 0x0004) ? true : false;     // 1 = depth first, 0 = breadth first
    sprintf(str, "0x%08X: TD: (0x%08X)", item & ~0xF, td.dword0);
    if ((item & ~0xF) == (Bit32u) usbdbg_param1)
      state = true;
    cur = treeview_insert(treeview, next, str, state);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    if ((item & ~0xF) == (Bit32u) usbdbg_param1)
      gtk_tree_selection_select_iter(selection, cur);
    item = td.dword0;
    if (queue_addr != 0) {
      // if breadth first or last in the element list, move on to next queue item
      if (!depthbreadth || !USB_UHCI_IS_LINK_VALID(item)) {
        item = queue.horz;
        queue_addr = 0;
      }
    }
  }
}

static void uhci_queue_dialog(Bit32u addr)
{
  int ret;
  struct QUEUE queue;
  char buffer[COMMON_STR_SIZE];
  GtkWidget *mainVbox, *label[2], *entry[2], *checkbox[4];
  GtkWidget *Hframe, *Vframe, *grid[2];

  DEV_MEM_READ_PHYSICAL(addr, sizeof(struct QUEUE), (Bit8u*)&queue);
  GtkWidget *dialog =
    gtk_dialog_new_with_buttons("Queue", GTK_WINDOW(main_dialog), GTK_DIALOG_MODAL,
                                g_dgettext("gtk30", "_Save"), GTK_RESPONSE_OK,
                                g_dgettext("gtk30", "_Cancel"), GTK_RESPONSE_CANCEL,
                                NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog), 200, 250);
  // TODO: set focus
  mainVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), mainVbox, TRUE, TRUE, 2);
  Hframe = gtk_frame_new("Horizontal Pointer");
  gtk_box_pack_start(GTK_BOX(mainVbox), Hframe, FALSE, FALSE, 2);
  grid[0] = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(Hframe), grid[0]);
  label[0] = gtk_label_new("Pointer");
  gtk_grid_attach(GTK_GRID(grid[0]), label[0], 0, 0, 1, 1);
  entry[0] = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid[0]), entry[0], 1, 0, 1, 1);
  checkbox[0] = gtk_check_button_new_with_label("Queue");
  gtk_grid_attach(GTK_GRID(grid[0]), checkbox[0], 1, 1, 1, 1);
  checkbox[1] = gtk_check_button_new_with_label("Terminate");
  gtk_grid_attach(GTK_GRID(grid[0]), checkbox[1], 1, 2, 1, 1);
  Vframe = gtk_frame_new("Vertical Pointer");
  gtk_box_pack_start(GTK_BOX(mainVbox), Vframe, FALSE, FALSE, 2);
  grid[1] = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(Vframe), grid[1]);
  label[1] = gtk_label_new("Pointer");
  gtk_grid_attach(GTK_GRID(grid[1]), label[1], 0, 0, 1, 1);
  entry[1] = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid[1]), entry[1], 1, 0, 1, 1);
  checkbox[2] = gtk_check_button_new_with_label("Queue");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[2], 1, 1, 1, 1);
  checkbox[3] = gtk_check_button_new_with_label("Terminate");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[3], 1, 2, 1, 1);
  // Set values
  sprintf(buffer, "0x%04X", queue.horz & ~0xF);
  gtk_entry_set_text(GTK_ENTRY(entry[0]), buffer);
  if (queue.horz & 2) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[0]), TRUE);
  }
  if (queue.horz & 1) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[1]), TRUE);
  }
  sprintf(buffer, "0x%04X", queue.vert & ~0xF);
  gtk_entry_set_text(GTK_ENTRY(entry[1]), buffer);
  if (queue.vert & 2) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[2]), TRUE);
  }
  if (queue.vert & 1) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[3]), TRUE);
  }
  // Show dialog
  gtk_widget_show_all(dialog);
  ret = gtk_dialog_run(GTK_DIALOG(dialog));
  if (ret == GTK_RESPONSE_OK) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[0])));
    queue.horz = strtol(buffer, NULL, 0);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[0]))) {
      queue.horz |= 2;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[1]))) {
      queue.horz |= 1;
    }
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[1])));
    queue.vert = strtol(buffer, NULL, 0);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[2]))) {
      queue.vert |= 2;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[3]))) {
      queue.vert |= 1;
    }
    DEV_MEM_WRITE_PHYSICAL(addr, sizeof(struct QUEUE), (Bit8u*)&queue);
  }
  gtk_widget_destroy(dialog);
}

static void depth_breadth_sel(GtkWidget *widget, gpointer data)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
    gtk_button_set_label(GTK_BUTTON(widget), "(Depth first) Vf");
  } else {
    gtk_button_set_label(GTK_BUTTON(widget), "(Breadth first) Vf");
  }
}

static void dump_buffer(GtkWidget *widget, gpointer data)
{
  DUMP_PARAMS *params = (DUMP_PARAMS*)data;
  const char *msg;

  if (params->size > 512) {
    params->size = 512;
  }
  if (params->size > 0) {
    msg = "Buffer dump not implemented yet";
  } else {
    msg = "Nothing to do...";
  }
  GtkWidget* error = gtk_message_dialog_new(GTK_WINDOW(td_dialog),
    GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, msg);
  gtk_window_set_title(GTK_WINDOW(error), params->title);
  gtk_dialog_run(GTK_DIALOG(error));
  gtk_widget_destroy(error);
}

static void uhci_td_dialog(Bit32u addr)
{
  int ret, value;
  struct TD td;
  char buffer[COMMON_STR_SIZE];
  GtkWidget *mainVbox, *entry[7], *checkbox[16], *grid[4], *label[7];
  GtkWidget *Shbox, *Svbox, *Bhbox, *LPframe, *Sframe[2], *PHframe;
  GtkWidget *combo, *button;
  DUMP_PARAMS dump_params;

  DEV_MEM_READ_PHYSICAL(addr, sizeof(struct TD), (Bit8u*)&td);
  td_dialog =
    gtk_dialog_new_with_buttons("Transfer Descriptor", GTK_WINDOW(main_dialog), GTK_DIALOG_MODAL,
                                g_dgettext("gtk30", "_Save"), GTK_RESPONSE_OK,
                                g_dgettext("gtk30", "_Cancel"), GTK_RESPONSE_CANCEL,
                                NULL);
  if (usbdbg_param2 & USB_LPARAM_FLAG_BEFORE) {
    gtk_window_set_title(GTK_WINDOW(td_dialog), "Transfer Descriptor: *Before*");
  } else if (usbdbg_param2 & USB_LPARAM_FLAG_AFTER) {
    gtk_window_set_title(GTK_WINDOW(td_dialog), "Transfer Descriptor: *After*");
  }
  gtk_window_set_default_size(GTK_WINDOW(td_dialog), 250, 300);
  mainVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(td_dialog))), mainVbox, TRUE, TRUE, 2);
  LPframe = gtk_frame_new("Link Pointer");
  gtk_box_pack_start(GTK_BOX(mainVbox), LPframe, FALSE, FALSE, 2);
  grid[0] = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(LPframe), grid[0]);
  label[0] = gtk_label_new("Link Pointer");
  gtk_grid_attach(GTK_GRID(grid[0]), label[0], 0, 0, 1, 1);
  entry[0] = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid[0]), entry[0], 1, 0, 1, 1);
  checkbox[0] = gtk_check_button_new_with_label("(Breadth first) Vf");
  g_signal_connect(checkbox[0], "clicked", G_CALLBACK(depth_breadth_sel), NULL);
  gtk_grid_attach(GTK_GRID(grid[0]), checkbox[0], 1, 1, 1, 1);
  checkbox[1] = gtk_check_button_new_with_label("Queue");
  gtk_grid_attach(GTK_GRID(grid[0]), checkbox[1], 1, 2, 1, 1);
  checkbox[2] = gtk_check_button_new_with_label("Terminate");
  gtk_grid_attach(GTK_GRID(grid[0]), checkbox[2], 1, 3, 1, 1);
  Sframe[0] = gtk_frame_new("Status");
  gtk_box_pack_start(GTK_BOX(mainVbox), Sframe[0], FALSE, FALSE, 2);
  Svbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(Sframe[0]), Svbox);
  Shbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(Svbox), Shbox, FALSE, FALSE, 2);
  label[1] = gtk_label_new("Actual Len");
  gtk_box_pack_start(GTK_BOX(Shbox), label[1], TRUE, TRUE, 2);
  entry[1] = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(Shbox), entry[1], TRUE, TRUE, 2);
  Sframe[1] = gtk_frame_new("Status");
  gtk_box_pack_start(GTK_BOX(Svbox), Sframe[1], FALSE, FALSE, 2);
  grid[1] = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(Sframe[1]), grid[1]);
  checkbox[3] = gtk_check_button_new_with_label("Active");
  if ((usbdbg_param2 & USB_LPARAM_FLAG_AFTER) == 0) {
    gtk_widget_set_sensitive(checkbox[3], 0);
  }
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[3], 0, 0, 1, 1);
  checkbox[4] = gtk_check_button_new_with_label("Stalled");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[4], 0, 1, 1, 1);
  checkbox[5] = gtk_check_button_new_with_label("Data Buffer Error");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[5], 0, 2, 1, 1);
  checkbox[6] = gtk_check_button_new_with_label("Babble Detect");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[6], 0, 3, 1, 1);
  checkbox[7] = gtk_check_button_new_with_label("NAK Received");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[7], 1, 0, 1, 1);
  checkbox[8] = gtk_check_button_new_with_label("CRC / Timeout");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[8], 1, 1, 1, 1);
  checkbox[9] = gtk_check_button_new_with_label("Bitstuff Error");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[9], 1, 2, 1, 1);
  checkbox[10] = gtk_check_button_new_with_label("Reserved");
  gtk_grid_attach(GTK_GRID(grid[1]), checkbox[10], 1, 3, 1, 1);
  grid[2] = gtk_grid_new();
  gtk_box_pack_start(GTK_BOX(Svbox), grid[2], FALSE, FALSE, 2);
  checkbox[11] = gtk_check_button_new_with_label("Interrupt on Comp.");
  gtk_grid_attach(GTK_GRID(grid[2]), checkbox[11], 0, 0, 1, 1);
  checkbox[12] = gtk_check_button_new_with_label("Isosynchronus");
  gtk_grid_attach(GTK_GRID(grid[2]), checkbox[12], 0, 1, 1, 1);
  checkbox[13] = gtk_check_button_new_with_label("Low Speed");
  gtk_grid_attach(GTK_GRID(grid[2]), checkbox[13], 2, 0, 1, 1);
  checkbox[14] = gtk_check_button_new_with_label("Short Packet");
  gtk_grid_attach(GTK_GRID(grid[2]), checkbox[14], 2, 1, 1, 1);
  label[2] = gtk_label_new("C_ERR");
  gtk_grid_attach(GTK_GRID(grid[2]), label[2], 1, 2, 1, 1);
  entry[2] = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid[2]), entry[2], 2, 2, 1, 1);
  PHframe = gtk_frame_new("Packet Header");
  gtk_box_pack_start(GTK_BOX(mainVbox), PHframe, FALSE, FALSE, 2);
  grid[3] = gtk_grid_new();
  gtk_container_add(GTK_CONTAINER(PHframe), grid[3]);
  label[3] = gtk_label_new("PID");
  gtk_grid_attach(GTK_GRID(grid[3]), label[3], 1, 0, 1, 1);
  combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "IN (0x69)");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "OUT (0xE1)");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "SETUP (0x2D)");
  gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), NULL, "ERROR");
  gtk_grid_attach(GTK_GRID(grid[3]), combo, 2, 0, 1, 1);
  label[4] = gtk_label_new("Device Address");
  gtk_grid_attach(GTK_GRID(grid[3]), label[4], 1, 1, 1, 1);
  entry[3] = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid[3]), entry[3], 2, 1, 1, 1);
  label[5] = gtk_label_new("End Point");
  gtk_grid_attach(GTK_GRID(grid[3]), label[5], 1, 2, 1, 1);
  entry[4] = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid[3]), entry[4], 2, 2, 1, 1);
  label[6] = gtk_label_new("Max Length");
  gtk_grid_attach(GTK_GRID(grid[3]), label[6], 1, 3, 1, 1);
  entry[5] = gtk_entry_new();
  gtk_grid_attach(GTK_GRID(grid[3]), entry[5], 2, 3, 1, 1);
  checkbox[15] = gtk_check_button_new_with_label("Toggle Bit");
  gtk_grid_attach(GTK_GRID(grid[3]), checkbox[15], 0, 2, 1, 1);
  Bhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(mainVbox), Bhbox, FALSE, FALSE, 2);
  label[7] = gtk_label_new("Buffer");
  gtk_box_pack_start(GTK_BOX(Bhbox), label[7], FALSE, FALSE, 2);
  entry[6] = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(Bhbox), entry[6], FALSE, FALSE, 2);
  button = gtk_button_new_with_label(">");
  gtk_box_pack_start(GTK_BOX(Bhbox), button, FALSE, FALSE, 2);
  // Set values
  sprintf(buffer, "0x%04X", td.dword0 & ~0xF);
  gtk_entry_set_text(GTK_ENTRY(entry[0]), buffer);
  if (td.dword0 & 4) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[0]), TRUE);
  }
  if (td.dword0 & 2) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[1]), TRUE);
  }
  if (td.dword0 & 1) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[2]), TRUE);
  }
  sprintf(buffer, "%i", td.dword1 & 0x3FF);
  gtk_entry_set_text(GTK_ENTRY(entry[1]), buffer);
  if (td.dword1 & (1 << 23)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[3]), TRUE);
  }
  if (td.dword1 & (1 << 22)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[4]), TRUE);
  }
  if (td.dword1 & (1 << 21)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[5]), TRUE);
  }
  if (td.dword1 & (1 << 20)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[6]), TRUE);
  }
  if (td.dword1 & (1 << 19)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[7]), TRUE);
  }
  if (td.dword1 & (1 << 18)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[8]), TRUE);
  }
  if (td.dword1 & (1 << 17)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[9]), TRUE);
  }
  if (td.dword1 & (1 << 16)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[10]), TRUE);
  }
  if (td.dword1 & (1 << 24)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[11]), TRUE);
  }
  if (td.dword1 & (1 << 25)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[12]), TRUE);
  }
  if (td.dword1 & (1 << 26)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[13]), TRUE);
  }
  if (td.dword1 & (1 << 29)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[14]), TRUE);
  }
  sprintf(buffer, "%i", (td.dword1 >> 27) & 3);
  gtk_entry_set_text(GTK_ENTRY(entry[2]), buffer);
  switch (td.dword2 & 0xff) {
    case 0x69:
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
      break;
    case 0xE1:
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 1);
      break;
    case 0x2D:
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 2);
      break;
    default:
      gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 3);
  }
  sprintf(buffer, "%i", (td.dword2 >> 8) & 0x7F);
  gtk_entry_set_text(GTK_ENTRY(entry[3]), buffer);
  sprintf(buffer, "%i", (td.dword2 >> 15) & 0x0F);
  gtk_entry_set_text(GTK_ENTRY(entry[4]), buffer);
  sprintf(buffer, "%i", (td.dword2 >> 21) & 0x7FF);
  gtk_entry_set_text(GTK_ENTRY(entry[5]), buffer);
  if (td.dword2 & (1 << 19)) {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbox[15]), TRUE);
  }
  sprintf(buffer, "0x%08X", td.dword3);
  gtk_entry_set_text(GTK_ENTRY(entry[6]), buffer);
  strcpy(dump_params.title, "UHCI: Transfer Descriptor Buffer");
  dump_params.address = td.dword3;
  if (usbdbg_param2 & USB_LPARAM_FLAG_AFTER) {
    dump_params.size = (td.dword1 & 0x3FF) + 1;
  } else {
    dump_params.size = ((td.dword2 >> 21) + 1) & 0x7FF;
  }
  dump_params.big = 0;
  g_signal_connect(button, "clicked", G_CALLBACK(dump_buffer), &dump_params);
  // Show dialog
  gtk_widget_show_all(td_dialog);
  ret = gtk_dialog_run(GTK_DIALOG(td_dialog));
  if (ret == GTK_RESPONSE_OK) {
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[0])));
    td.dword0 = strtol(buffer, NULL, 0);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[0]))) {
      td.dword0 |= 4;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[1]))) {
      td.dword0 |= 2;
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[2]))) {
      td.dword0 |= 1;
    }
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[1])));
    td.dword1 = strtol(buffer, NULL, 0) & 0x3FF;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[3]))) {
      td.dword1 |= (1 << 23);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[4]))) {
      td.dword1 |= (1 << 22);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[5]))) {
      td.dword1 |= (1 << 21);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[6]))) {
      td.dword1 |= (1 << 20);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[7]))) {
      td.dword1 |= (1 << 19);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[8]))) {
      td.dword1 |= (1 << 18);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[9]))) {
      td.dword1 |= (1 << 17);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[10]))) {
      td.dword1 |= (1 << 16);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[11]))) {
      td.dword1 |= (1 << 24);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[12]))) {
      td.dword1 |= (1 << 25);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[13]))) {
      td.dword1 |= (1 << 26);
    }
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox[14]))) {
      td.dword1 |= (1 << 29);
    }
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[2])));
    td.dword1 |= ((strtol(buffer, NULL, 0) & 3) << 27);
    value = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
    switch (value) {
      case 0:
        td.dword2 = 0x69;
        break;
      case 1:
        td.dword2 = 0xE1;
        break;
      case 2:
        td.dword2 = 0x2D;
        break;
      default:
        td.dword2 = 0;
    }
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[3])));
    td.dword2 |= ((strtol(buffer, NULL, 0) & 0x7F) << 8);
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[4])));
    td.dword2 |= ((strtol(buffer, NULL, 0) & 0x0F) << 15);
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[5])));
    td.dword2 |= ((strtol(buffer, NULL, 0) & 0x7FF) << 21);
    strcpy(buffer, gtk_entry_get_text(GTK_ENTRY(entry[6])));
    td.dword3 = strtol(buffer, NULL, 0);
    DEV_MEM_WRITE_PHYSICAL(addr, sizeof(struct TD), (Bit8u*)&td);
  }
  gtk_widget_destroy(td_dialog);
}

static void uhci_display_td(GtkWidget *widget, gpointer data)
{
  GtkTreeSelection *selection;
  GtkTreeModel     *model;
  GtkTreeIter       iter;

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(data));
  if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
    gchar *name;

    gtk_tree_model_get(model, &iter, 0, &name, -1);
    Bit32u value = g_ascii_strtoull(name, NULL, 16);
    if (g_strstr_len(name, 14, "TD") == NULL) {
      uhci_queue_dialog(value);
    } else {
      uhci_td_dialog(value);
    }
    g_free(name);
  } else {
    GtkWidget* error = gtk_message_dialog_new(
      GTK_WINDOW(main_dialog), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
    "No TD selected");
    gtk_window_set_title(GTK_WINDOW(error), "WARNING");
    gtk_dialog_run(GTK_DIALOG(error));
    gtk_widget_destroy(error);
  }
}

// UHCI main dialog

int uhci_debug_dialog(int type, int param1)
{
  bx_list_c *UHCI_state = NULL;
  int i, ret;
  Bit32u frame_addr, frame_num;
  char buffer[COMMON_STR_SIZE];
  GtkWidget *mainVbox, *BAhbox, *hbox[2], *vbox[3], *ro_entry[4];
  GtkWidget *ORframe, *ORhbox, *ORvbox[3];
  GtkWidget *PRframe, *PRhbox, *PRvbox[3];
  GtkWidget *TVvbox, *FNhbox;
  GtkWidget *button[8], *FNlabel, *treeview;
  usb_reg_t uhci_reg_def[5] = {
    {"Command Register", NULL, 4, attribs_u_command},
    {"Status Register", NULL, 4, attribs_u_status},
    {"Interrupt Enable Register", NULL, 4, attribs_u_interrupt},
    {"Port 0 Register", NULL, 4, attribs_u_ports},
    {"Port 1 Register", NULL, 4, attribs_u_ports}
  };

  UHCI_state = (bx_list_c*)SIM->get_param("usb_uhci", SIM->get_bochs_root());
  if (UHCI_state == NULL)
    return -1;

  main_dialog = gtk_dialog_new();
  switch (type) {
    case USB_DEBUG_FRAME:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "UHCI Debug Dialog: Break Type: Start of Frame");
      break;
    case USB_DEBUG_COMMAND:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "UHCI Debug Dialog: Break Type: Doorbell");
      break;
    case USB_DEBUG_NONEXIST:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "UHCI Debug Dialog: Break Type: Non-Existant Port Write");
      break;
    case USB_DEBUG_RESET:
      sprintf(buffer, "UHCI Debug Dialog: Break Type: Port %d Reset", param1);
      gtk_window_set_title(GTK_WINDOW(main_dialog), buffer);
      break;
    case USB_DEBUG_ENABLE:
      sprintf(buffer, "UHCI Debug Dialog: Break Type: Port %d Enable", param1);
      gtk_window_set_title(GTK_WINDOW(main_dialog), buffer);
      break;
    default:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "UHCI Debug Dialog");
  }
  gtk_window_set_default_size(GTK_WINDOW(main_dialog), 600, 500);
  button[0] = gtk_dialog_add_button(GTK_DIALOG(main_dialog), "Continue", GTK_RESPONSE_OK);
  button[1] = gtk_dialog_add_button(GTK_DIALOG(main_dialog), "Quit", GTK_RESPONSE_CANCEL);
  gtk_dialog_set_default_response(GTK_DIALOG(main_dialog), GTK_RESPONSE_OK);
  gtk_window_set_focus(GTK_WINDOW(main_dialog), button[0]);
  mainVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(main_dialog))), mainVbox, TRUE, TRUE, 2);

  BAhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(mainVbox), BAhbox, FALSE, FALSE, 2);
  usbdlg_create_label(BAhbox, "UHCI at Base IO address", false);
  ro_entry[0] = usbdlg_create_ro_entry(BAhbox, false);

  hbox[0] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(mainVbox), hbox[0], TRUE, TRUE, 2);
  hbox[1] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(mainVbox), hbox[1], TRUE, TRUE, 2);
  for (i = 0; i < 3; i++) {
    vbox[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox[0]), vbox[i], TRUE, TRUE, 2);
  }

  ORframe = gtk_frame_new("Operational Registers");
  gtk_box_pack_start(GTK_BOX(vbox[0]), ORframe, FALSE, FALSE, 2);
  ORhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(ORframe), ORhbox);
  for (i = 0; i < 3; i++) {
    ORvbox[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(ORhbox), ORvbox[i], FALSE, FALSE, 2);
  }
  uhci_entry[UHCI_REG_COMMAND] = usbdlg_create_entry_with_label(ORvbox, "Command");
  button[2] = gtk_button_new_with_label("<>");
  uhci_reg_def[0].entry = uhci_entry[UHCI_REG_COMMAND];
  g_signal_connect(button[2], "clicked", G_CALLBACK(usb_regview_dialog), &uhci_reg_def[0]);
  gtk_box_pack_start(GTK_BOX(ORvbox[2]), button[2], FALSE, FALSE, 2);
  uhci_entry[UHCI_REG_STATUS] = usbdlg_create_entry_with_label(ORvbox, "Status");
  button[3] = gtk_button_new_with_label("<>");
  uhci_reg_def[1].entry = uhci_entry[UHCI_REG_STATUS];
  g_signal_connect(button[3], "clicked", G_CALLBACK(usb_regview_dialog), &uhci_reg_def[1]);
  gtk_box_pack_start(GTK_BOX(ORvbox[2]), button[3], FALSE, FALSE, 2);
  uhci_entry[UHCI_REG_INTERRUPT] = usbdlg_create_entry_with_label(ORvbox, "Interrupt Enable");
  button[4] = gtk_button_new_with_label("<>");
  uhci_reg_def[2].entry = uhci_entry[UHCI_REG_INTERRUPT];
  g_signal_connect(button[4], "clicked", G_CALLBACK(usb_regview_dialog), &uhci_reg_def[2]);
  gtk_box_pack_start(GTK_BOX(ORvbox[2]), button[4], FALSE, FALSE, 2);
  uhci_entry[UHCI_REG_FRAME_NUM] = usbdlg_create_entry_with_label(ORvbox, "Frame Number");
  uhci_entry[UHCI_REG_FRAME_ADDR] = usbdlg_create_entry_with_label(ORvbox, "Frame Address");
  uhci_entry[UHCI_REG_SOF] = usbdlg_create_entry_with_label(ORvbox, "Start of Frame");

  PRframe = gtk_frame_new("Port Registers");
  gtk_box_pack_start(GTK_BOX(hbox[1]), PRframe, FALSE, FALSE, 2);
  PRhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(PRframe), PRhbox);
  for (i = 0; i < 3; i++) {
    PRvbox[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(PRhbox), PRvbox[i], FALSE, FALSE, 2);
  }
  for (i = 0; i < 2; i++) {
    sprintf(buffer, "Port %d", i);
    uhci_entry[UHCI_REG_PORT0 + i] = usbdlg_create_entry_with_label(PRvbox, buffer);
    ro_entry[i + 1] = usbdlg_create_ro_entry_with_label(PRvbox, "Emulation Type");
    button[i + 5] = gtk_button_new_with_label("<>");
    uhci_reg_def[i + 3].entry = uhci_entry[UHCI_REG_PORT0 + i];
    g_signal_connect(button[i + 5], "clicked", G_CALLBACK(usb_regview_dialog), &uhci_reg_def[i + 3]);
    gtk_box_pack_start(GTK_BOX(PRvbox[2]), button[i + 5], FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(PRvbox[2]), gtk_label_new(" "), FALSE, FALSE, 8); // spacer
  }
  TVvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(hbox[1]), TVvbox, TRUE, TRUE, 2);
  FNhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(TVvbox), FNhbox, FALSE, FALSE, 2);
  FNlabel = usbdlg_create_label(FNhbox, "Frame at Address", false);
  ro_entry[3] = usbdlg_create_ro_entry(FNhbox, false);
  button[7] = gtk_button_new_with_label("View Item");
  gtk_widget_set_sensitive(button[7], 0);
  gtk_box_pack_start(GTK_BOX(FNhbox), button[7], FALSE, FALSE, 2);
  treeview = gtk_tree_view_new();
  gtk_box_pack_start(GTK_BOX(TVvbox), treeview, TRUE, TRUE, 2);

  usbdlg_create_apply_button(mainVbox);
  usbdlg_create_debug_flags(vbox[2]);
  // Set values
  pci_bar_address = get_pci_bar_addr((bx_shadow_data_c*)SIM->get_param("hub.pci_conf", UHCI_state), 4);
  sprintf(buffer, "0x%04X", pci_bar_address);
  gtk_entry_set_text(GTK_ENTRY(ro_entry[0]), buffer);
  sprintf(buffer, "0x%04X", usb_io_read(pci_bar_address + 0, 2));
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_COMMAND]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_COMMAND]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_COMMAND]);
  sprintf(buffer, "0x%04X", usb_io_read(pci_bar_address + 2, 2));
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_STATUS]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_STATUS]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_STATUS]);
  sprintf(buffer, "0x%04X", usb_io_read(pci_bar_address + 4, 2));
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_INTERRUPT]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_INTERRUPT]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_INTERRUPT]);
  frame_num = usb_io_read(pci_bar_address + 6, 2);
  sprintf(buffer, "0x%04X", frame_num);
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_FRAME_NUM]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_FRAME_NUM]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_FRAME_NUM]);
  frame_addr = usb_io_read(pci_bar_address + 8, 4);
  sprintf(buffer, "0x%08X", frame_addr);
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_FRAME_ADDR]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_FRAME_ADDR]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_FRAME_ADDR]);
  sprintf(buffer, "0x%02X", usb_io_read(pci_bar_address + 12, 1));
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_SOF]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_SOF]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_SOF]);
  sprintf(buffer, "0x%04X", usb_io_read(pci_bar_address + 16, 2));
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_PORT0]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_PORT0]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_PORT0]);
  SIM->get_param_enum("port1.device", host_param)->dump_param(buffer, COMMON_STR_SIZE, 1);
  gtk_entry_set_text(GTK_ENTRY(ro_entry[1]), buffer);
  sprintf(buffer, "0x%04X", usb_io_read(pci_bar_address + 18, 2));
  gtk_entry_set_text(GTK_ENTRY(uhci_entry[UHCI_REG_PORT1]), buffer);
  g_signal_connect(GTK_EDITABLE(uhci_entry[UHCI_REG_PORT1]), "changed",
                   G_CALLBACK(on_entry_changed), &u_changed[UHCI_REG_PORT1]);
  SIM->get_param_enum("port2.device", host_param)->dump_param(buffer, COMMON_STR_SIZE, 1);
  gtk_entry_set_text(GTK_ENTRY(ro_entry[2]), buffer);

  frame_addr += (frame_num * sizeof(Bit32u));
  sprintf(buffer, "0x%08X", frame_addr);
  gtk_entry_set_text(GTK_ENTRY(ro_entry[3]), buffer);

  bool valid = 0;
  switch (type) {
    // The DoTransfer() function was called
    case USB_DEBUG_COMMAND:
    // The start of a frame timer was triggered
    case USB_DEBUG_FRAME:
      gtk_label_set_text(GTK_LABEL(FNlabel), "SOF Frame Address");
      if (frame_addr != 0x00000000) {
        hc_uhci_do_item(treeview, frame_addr, frame_num);
        gtk_tree_view_expand_all(GTK_TREE_VIEW(treeview));
        g_signal_connect(button[7], "clicked", G_CALLBACK(uhci_display_td), treeview);
        gtk_widget_set_sensitive(button[7], 1);
        valid = 1;
      }
      break;

    // an event triggered. We ignore these in the uhci
    //case USB_DEBUG_EVENT:
    //  break;

    // first byte (word, dword, qword) of first non-existant port was written to
    case USB_DEBUG_NONEXIST:
    // port reset (non-root reset)
    case USB_DEBUG_RESET:
    // enable changed
    case USB_DEBUG_ENABLE:
      gtk_label_set_text(GTK_LABEL(FNlabel), "None");
      gtk_entry_set_text(GTK_ENTRY(ro_entry[3]), "None");
      break;
  }

  if (!valid) {
    gtk_widget_set_sensitive(treeview, 0);
    usbdlg_create_label(TVvbox, "This trigger does not populate the tree view", false);
  } else {
    usbdlg_create_label(TVvbox, "Tree view populated", false);
  }
  // Show dialog
  gtk_widget_show_all(main_dialog);
  ret = gtk_dialog_run(GTK_DIALOG(main_dialog));
  if (ret == GTK_RESPONSE_OK) {
    usbdlg_set_debug_flags();
  }
  gtk_widget_destroy(main_dialog);
  return ret;
}

// xHCI main dialog

int xhci_debug_dialog(int type, int param1)
{
  bx_list_c *xHCI_state = NULL;
  int i, n_ports, ret;
  Bit32u dword, offset;
  Bit64u RingPtr = 0;
  char buffer[COMMON_STR_SIZE], tmpbuf[32];
  GtkWidget *mainVbox, *BAhbox, *hbox[2], *vbox[4], *entry[38];
  GtkWidget *CRframe, *CRhbox, *CRvbox[2];
  GtkWidget *ORframe, *ORhbox, *ORvbox[2];
  GtkWidget *RTframe, *RThbox, *RTvbox[2];
  GtkWidget *PRframe, *PRhbox, *PRvbox[3];
  GtkWidget *TVvbox, *FNhbox;
  GtkWidget *button[13], *FNlabel, *treeview;
  usb_reg_t xhci_reg_def[10] = {
    {"Port 0 Register", NULL, 8, attribs_x_ports},
    {"Port 1 Register", NULL, 8, attribs_x_ports},
    {"Port 2 Register", NULL, 8, attribs_x_ports},
    {"Port 3 Register", NULL, 8, attribs_x_ports},
    {"Port 4 Register", NULL, 8, attribs_x_ports},
    {"Port 5 Register", NULL, 8, attribs_x_ports},
    {"Port 6 Register", NULL, 8, attribs_x_ports},
    {"Port 7 Register", NULL, 8, attribs_x_ports},
    {"Port 8 Register", NULL, 8, attribs_x_ports},
    {"Port 9 Register", NULL, 8, attribs_x_ports}
  };

  xHCI_state = (bx_list_c*)SIM->get_param("usb_xhci", SIM->get_bochs_root());
  if (xHCI_state == NULL)
    return -1;

  main_dialog = gtk_dialog_new();
  switch (type) {
    case USB_DEBUG_COMMAND:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "xHCI Debug Dialog: Break Type: Command Ring");
      break;
    case USB_DEBUG_EVENT:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "xHCI Debug Dialog: Break Type: Event Ring");
      break;
    case USB_DEBUG_FRAME:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "xHCI Debug Dialog: Break Type: Frame");
      break;
    default:
      gtk_window_set_title(GTK_WINDOW(main_dialog), "xHCI Debug Dialog");
  }
  gtk_window_set_default_size(GTK_WINDOW(main_dialog), 600, 500);
  button[0] = gtk_dialog_add_button(GTK_DIALOG(main_dialog), "Continue", GTK_RESPONSE_OK);
  button[1] = gtk_dialog_add_button(GTK_DIALOG(main_dialog), "Quit", GTK_RESPONSE_CANCEL);
  gtk_dialog_set_default_response(GTK_DIALOG(main_dialog), GTK_RESPONSE_OK);
  gtk_window_set_focus(GTK_WINDOW(main_dialog), button[0]);
  mainVbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(main_dialog))), mainVbox, TRUE, TRUE, 2);

  BAhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(mainVbox), BAhbox, FALSE, FALSE, 2);
  usbdlg_create_label(BAhbox, "xHCI at Base MMIO address", false);
  entry[0] = usbdlg_create_ro_entry(BAhbox, false);

  hbox[0] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(mainVbox), hbox[0], TRUE, TRUE, 2);
  hbox[1] = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(mainVbox), hbox[1], TRUE, TRUE, 2);
  for (i = 0; i < 4; i++) {
    vbox[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(hbox[0]), vbox[i], TRUE, TRUE, 2);
  }

  CRframe = gtk_frame_new("Capability Registers");
  gtk_box_pack_start(GTK_BOX(vbox[0]), CRframe, FALSE, FALSE, 2);
  CRhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(CRframe), CRhbox);
  CRvbox[0] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(CRhbox), CRvbox[0], FALSE, FALSE, 2);
  CRvbox[1] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(CRhbox), CRvbox[1], FALSE, FALSE, 2);
  entry[1] = usbdlg_create_entry_with_label(CRvbox, "Cap Length");
  entry[2] = usbdlg_create_entry_with_label(CRvbox, "HCSParams1");
  entry[3] = usbdlg_create_ro_entry_with_label(CRvbox, "HCSParams2");
  entry[4] = usbdlg_create_entry_with_label(CRvbox, "HCSParams3");
  entry[5] = usbdlg_create_entry_with_label(CRvbox, "HCCParams1");
  entry[6] = usbdlg_create_entry_with_label(CRvbox, "DB Offset");
  entry[7] = usbdlg_create_entry_with_label(CRvbox, "RTS Offset");
  entry[8] = usbdlg_create_entry_with_label(CRvbox, "HCCParams2");

  ORframe = gtk_frame_new("Operational Registers");
  gtk_box_pack_start(GTK_BOX(vbox[1]), ORframe, FALSE, FALSE, 2);
  ORhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(ORframe), ORhbox);
  ORvbox[0] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(ORhbox), ORvbox[0], FALSE, FALSE, 2);
  ORvbox[1] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(ORhbox), ORvbox[1], FALSE, FALSE, 2);
  entry[9] = usbdlg_create_ro_entry_with_label(ORvbox, "Command");
  entry[10] = usbdlg_create_entry_with_label(ORvbox, "Status");
  entry[11] = usbdlg_create_entry_with_label(ORvbox, "Page Size");
  entry[12] = usbdlg_create_entry_with_label(ORvbox, "Device Notification");
  entry[13] = usbdlg_create_entry_with_label(ORvbox, "Command Ring");
  entry[14] = usbdlg_create_entry_with_label(ORvbox, "Device Context Base");
  entry[15] = usbdlg_create_entry_with_label(ORvbox, "Configure");

  RTframe = gtk_frame_new("Runtime Registers");
  gtk_box_pack_start(GTK_BOX(vbox[2]), RTframe, FALSE, FALSE, 2);
  RThbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(RTframe), RThbox);
  RTvbox[0] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(RThbox), RTvbox[0], FALSE, FALSE, 2);
  RTvbox[1] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(RThbox), RTvbox[1], FALSE, FALSE, 2);
  entry[16] = usbdlg_create_entry_with_label(RTvbox, "Microframe Index");

  PRframe = gtk_frame_new("Port Registers");
  gtk_box_pack_start(GTK_BOX(hbox[1]), PRframe, FALSE, FALSE, 2);
  PRhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_container_add(GTK_CONTAINER(PRframe), PRhbox);
  for (i = 0; i < 3; i++) {
    PRvbox[i] = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(PRhbox), PRvbox[i], FALSE, FALSE, 2);
  }
  n_ports = SIM->get_param_num(BXPN_XHCI_N_PORTS)->get();
  for (i = 0; i < n_ports; i++) {
    sprintf(buffer, "Port %d", i);
    entry[i * 2 + 17] = usbdlg_create_entry_with_label(PRvbox, buffer);
    entry[i * 2 + 18] = usbdlg_create_ro_entry_with_label(PRvbox, "Emulation Type");
    button[i + 2] = gtk_button_new_with_label("<>");
    xhci_reg_def[i].entry = entry[i * 2 + 17];
    g_signal_connect(button[i + 2], "clicked", G_CALLBACK(usb_regview_dialog), &xhci_reg_def[i]);
    gtk_box_pack_start(GTK_BOX(PRvbox[2]), button[i + 2], FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(PRvbox[2]), gtk_label_new(" "), FALSE, FALSE, 8); // spacer
  }
  TVvbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(hbox[1]), TVvbox, TRUE, TRUE, 2);
  FNhbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(TVvbox), FNhbox, FALSE, FALSE, 2);
  FNlabel = usbdlg_create_label(FNhbox, "Ring Address", false);
  entry[n_ports * 2 + 17] = usbdlg_create_ro_entry(FNhbox, false);
  button[12] = gtk_button_new_with_label("View TRB");
  gtk_widget_set_sensitive(button[12], 0);
  gtk_box_pack_start(GTK_BOX(FNhbox), button[12], FALSE, FALSE, 2);
  treeview = gtk_tree_view_new();
  gtk_box_pack_start(GTK_BOX(TVvbox), treeview, TRUE, TRUE, 2);

  usbdlg_create_apply_button(mainVbox);
  usbdlg_create_debug_flags(vbox[3]);
  // Set values
  pci_bar_address = get_pci_bar_addr((bx_shadow_data_c*)SIM->get_param("hub.pci_conf", xHCI_state), 0);
  sprintf(buffer, "0x%08X", pci_bar_address);
  gtk_entry_set_text(GTK_ENTRY(entry[0]), buffer);
  for (i = 0; i < 8; i++) {
    dword = xhci_read_dword(pci_bar_address + (i * 4));
    sprintf(buffer, "0x%08X", dword);
    gtk_entry_set_text(GTK_ENTRY(entry[i + 1]), buffer);
    g_signal_connect(GTK_EDITABLE(entry[i + 1]), "changed", G_CALLBACK(on_entry_changed), NULL);
  }
  offset = xhci_read_dword(pci_bar_address + 0) & 0xFF;

  dword = xhci_read_dword(pci_bar_address + offset + 0x00);
  sprintf(buffer, "0x%08X", dword);
  gtk_entry_set_text(GTK_ENTRY(entry[9]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[9]), "changed", G_CALLBACK(on_entry_changed), NULL);
  dword = xhci_read_dword(pci_bar_address + offset + 0x04);
  sprintf(buffer, "0x%08X", dword);
  gtk_entry_set_text(GTK_ENTRY(entry[10]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[10]), "changed", G_CALLBACK(on_entry_changed), NULL);
  dword = xhci_read_dword(pci_bar_address + offset + 0x08);
  sprintf(buffer, "0x%08X", dword);
  gtk_entry_set_text(GTK_ENTRY(entry[11]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[11]), "changed", G_CALLBACK(on_entry_changed), NULL);
  dword = xhci_read_dword(pci_bar_address + offset + 0x14);
  sprintf(buffer, "0x%08X", dword);
  gtk_entry_set_text(GTK_ENTRY(entry[12]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[12]), "changed", G_CALLBACK(on_entry_changed), NULL);
  // we can't read this using DEV_MEM_READ_PHYSICAL since the handler will return zero
  sprintf(buffer, "0x" FMT_ADDRX64, SIM->get_param_num("hub.op_regs.HcCrcr.actual", xHCI_state)->get64());
  gtk_entry_set_text(GTK_ENTRY(entry[13]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[13]), "changed", G_CALLBACK(on_entry_changed), NULL);
  Bit64u qword = xhci_read_dword(pci_bar_address + offset + 0x30) |
   ((Bit64u) xhci_read_dword(pci_bar_address + offset + 0x34) << 32);
  sprintf(buffer, "0x" FMT_ADDRX64, qword);
  gtk_entry_set_text(GTK_ENTRY(entry[14]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[14]), "changed", G_CALLBACK(on_entry_changed), NULL);
  dword = xhci_read_dword(pci_bar_address + offset + 0x38);
  sprintf(buffer, "0x%08X", dword);
  gtk_entry_set_text(GTK_ENTRY(entry[15]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[15]), "changed", G_CALLBACK(on_entry_changed), NULL);

  offset = xhci_read_dword(pci_bar_address + 0x18);
  dword = xhci_read_dword(pci_bar_address + offset + 0);
  sprintf(buffer, "0x%08X", dword);
  gtk_entry_set_text(GTK_ENTRY(entry[16]), buffer);
  g_signal_connect(GTK_EDITABLE(entry[16]), "changed", G_CALLBACK(on_entry_changed), NULL);
  // show up to 10 port register sets
  for (i = 0; i < n_ports; i++) {
    dword = xhci_read_dword(pci_bar_address + XHCI_PORT_SET_OFFSET + (i * 16));
    sprintf(buffer, "0x%08X", dword);
    gtk_entry_set_text(GTK_ENTRY(entry[i * 2 + 17]), buffer);
    g_signal_connect(GTK_EDITABLE(entry[i * 2 + 17]), "changed", G_CALLBACK(on_entry_changed), NULL);
    sprintf(tmpbuf, "port%d.device", i + 1);
    SIM->get_param_enum(tmpbuf, host_param)->dump_param(buffer, COMMON_STR_SIZE, 1);
    gtk_entry_set_text(GTK_ENTRY(entry[i * 2 + 18]), buffer);
  }

  bool valid = 0;
  switch (type) {
    // a command TRB was placed on the command ring
    case USB_DEBUG_COMMAND:
      gtk_label_set_text(GTK_LABEL(FNlabel), "Command Ring Address");
      RingPtr = SIM->get_param_num("hub.op_regs.HcCrcr.crc", xHCI_state)->get();
      sprintf(buffer, "0x" FMT_ADDRX64, RingPtr);
      gtk_entry_set_text(GTK_ENTRY(entry[n_ports * 2 + 17]), buffer);
      if (RingPtr != 0) {
        gtk_widget_set_sensitive(button[12], 1);
        valid = 1;
      }
      break;

    // an event TRB was placed on an event ring
    case USB_DEBUG_EVENT:
      gtk_label_set_text(GTK_LABEL(FNlabel), "Event Ring");
      sprintf(buffer, "Interrupter %i", param1);
      gtk_entry_set_text(GTK_ENTRY(entry[n_ports * 2 + 17]), buffer);
      gtk_widget_set_sensitive(button[12], 1);
      valid = 1;
      break;

    case USB_DEBUG_FRAME:

      gtk_label_set_text(GTK_LABEL(FNlabel), "SOF Frame Address");

      break;

    // first byte (word, dword, qword) of first non-existant port was written to
    case USB_DEBUG_NONEXIST:
    // port reset (non-root reset)
    case USB_DEBUG_RESET:
    // enable changed
    case USB_DEBUG_ENABLE:
      gtk_label_set_text(GTK_LABEL(FNlabel), "None");
      gtk_entry_set_text(GTK_ENTRY(entry[n_ports * 2 + 17]), "None");
      break;
  }

  if (!valid) {
    gtk_widget_set_sensitive(treeview, 0);
    usbdlg_create_label(TVvbox, "This trigger does not populate the tree view", false);
  } else {
    usbdlg_create_label(TVvbox, "Tree view populated", false);
  }
  // Show dialog
  gtk_widget_show_all(main_dialog);
  ret = gtk_dialog_run(GTK_DIALOG(main_dialog));
  if (ret == GTK_RESPONSE_OK) {
    usbdlg_set_debug_flags();
  }
  gtk_widget_destroy(main_dialog);
  return ret;
}

// USB debug dialog entry point

int usb_debug_dialog(int type, int param1, int param2)
{
  static bool first_call = true;
  int argc = 1, ret;
  char *argv[2], **argvp, buffer[COMMON_STR_SIZE];

  if (first_call) {
    if (!SIM->is_wx_selected() && !SIM->has_debug_gui()) {
      // you MUST call gtk_init, even with faked arguments, because it inits GDK and Glib
      *buffer = 0;        // gtk flames out if you pass in a NULL -- sheesh
      argv[0] = buffer;   // so I really do have to fake up an "argv" list
      argv[1] = NULL;
      argvp = argv;
      if (gtk_init_check(&argc, &argvp) == false) {
        fprintf(stderr, "gtk init failed, can not access display?\n");
        return -1;
      }
      MakeGTKthreads();
    }
    first_call = false;
  }
  host_param = SIM->get_param(hc_param_str[usb_debug_type]);
  usbdbg_param1 = param1;
  usbdbg_param2 = param2;
  if (usb_debug_type == USB_DEBUG_UHCI) {
    ret = uhci_debug_dialog(type, param1);
    return (ret == GTK_RESPONSE_OK) ? 0 : -1;
  } else if (usb_debug_type == USB_DEBUG_XHCI) {
    ret = xhci_debug_dialog(type, param1);
    return (ret == GTK_RESPONSE_OK) ? 0 : -1;
  } else {
    GtkWidget* error = gtk_message_dialog_new(
        NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK_CANCEL,
        "GTK version of USB debugger dialog not implemented yet");
    gtk_window_set_title(GTK_WINDOW(error), "WARNING");
    ret = gtk_dialog_run(GTK_DIALOG(error));
    gtk_widget_destroy(error);
    return (ret == GTK_RESPONSE_OK) ? 0 : -1;
  }
}

#endif
