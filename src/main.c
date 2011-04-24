/*
 * Copyright (C) 2008 University of South Australia
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.  You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <gtk/gtk.h>
#include <stdlib.h>

/* References used in the tree model to store data.
   Each enum references the column the device is being stored at. */
enum {
    COL_ID = 0, /* device id, int*/
    COL_NAME,   /* device name, string */
    COL_USE,    /* use field as of XListInputDevices */
    COL_ICON,   /* icon */
    COL_GENERATION,  /* increased in every query_devices */
    NUM_COLS
};

enum {
    ICON_MOUSE,
    ICON_KEYBOARD,
    ICON_FLOATING
};

typedef struct {
    Display     *dpy;       /* Display connection (in addition to GTK) */
    GList       *changes;   /* changes to be applied when "apply" is hit */
    GtkTreeView *treeview;  /* the main view */
    GtkWidget   *window;
    gint         generation;
} GDeviceSetup;

/* Forward declarations */
static GtkTreeStore* query_devices(GDeviceSetup *gds);

/* Xlib */
static Display* dpy_init()
{
    Display           *dpy;
    int opcode, event, error;
    int major = 2, minor = 0; /* XInput 2.0 */

    dpy = XOpenDisplay(NULL);
    if (!dpy)
    {
        g_debug("Unable to open display.\n");
        return NULL;
    }

    /* XInput Extension available? */
    if (!XQueryExtension(dpy, "XInputExtension", &opcode, &event, &error))
      {
	g_debug("X Input extension not available.\n");
	return NULL;
      }

    /* Which version of XI? */
    if (XIQueryVersion(dpy, &major, &minor) == BadRequest)
      {
	g_debug("XI2 not available. Server supports %d.%d\n", major, minor);
	return NULL;
      }

    return dpy;
}

/**
 * Assemble a change attachment struct and return it. Must be freed by the
 * caller. Struct can be used to apply multiple changes at once.
 */
static int change_attachment(GDeviceSetup *gds, int id, int id_to)
{
    XIAttachSlaveInfo att;
    int ret;

    att.type = XIAttachSlave;
    att.deviceid = id;
    att.new_master = id_to;

    ret = XIChangeHierarchy(gds->dpy, (XIAnyHierarchyChangeInfo*)&att, 1);
    return ret;
}

/**
 * Remove a master device from the display. All SDs attached to dev will be
 * set to floating.
 * Effective immediately.
 */
static void remove_master(GDeviceSetup *gds, int id)
{
    XIRemoveMasterInfo remove;

    remove.type = XIRemoveMaster;
    remove.deviceid = id;
    remove.return_mode = XIFloating;
    XIChangeHierarchy(gds->dpy, (XIAnyHierarchyChangeInfo*)&remove, 1);
    XFlush(gds->dpy);
}

/**
 * Create a master device with the given name on the display. Applied
 * immediately.
 */
static void create_master(GDeviceSetup *gds, const char* name)
{
    XIAddMasterInfo cr;

    cr.type = XIAddMaster;
    cr.name = (char*)name;
    cr.send_core = TRUE;
    cr.enable = TRUE;

    XIChangeHierarchy(gds->dpy, (XIAnyHierarchyChangeInfo*)&cr, 1);
    XFlush(gds->dpy);
}

static void toggle_cancelapply_buttons(GDeviceSetup* gds, int enable)
{
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gds->window),
                                      GTK_RESPONSE_APPLY, enable);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(gds->window),
                                      GTK_RESPONSE_CANCEL, enable);
}

/**
 * Drag-and-drop received.
 */
static void signal_dnd_recv(GtkTreeView *tv,
                            GdkDragContext *context,
                            int x, int y,
                            GtkSelectionData *selection,
                            guint info, guint time,
                            gpointer data)
{
    GDeviceSetup *gds;
    GtkTreeModel *model;
    GtkTreeSelection *sel;
    GtkTreeIter sel_iter, dest_iter, parent, ins_iter,
                *final_parent, *final_sib;
    GtkTreePath *path;
    GtkTreeViewDropPosition pos;
    gchar *name, *md_name;
    int id, md_id;
    int use, md_use;

    gds = (GDeviceSetup*)data;
    model = gtk_tree_view_get_model(tv);
    sel = gtk_tree_view_get_selection(tv);
    if (!gtk_tree_selection_get_selected(sel, NULL, &sel_iter))
        return;

    gtk_tree_model_get(model, &sel_iter,
                       COL_NAME, &name,
                       COL_ID, &id,
                       COL_USE, &use,
                       -1);

    /* MD selected? */
    if (use == XIMasterPointer || use == XIMasterKeyboard)
        return;

    if (!gtk_tree_view_get_dest_row_at_pos(tv, x, y, &path, &pos))
        return;

    gtk_tree_model_get_iter(model, &dest_iter, path);

    /* check for parent, set final_parent to parent and final_sib to the
     * sibling we're dropping onto. */
    if (!gtk_tree_model_iter_parent(model, &parent, &dest_iter))
    {
        final_parent = &dest_iter;
        final_sib = NULL;
    } else
    {
        final_parent = &parent;
        final_sib = &dest_iter;
    }

    gtk_tree_model_get(GTK_TREE_MODEL(model), final_parent,
                       COL_NAME, &md_name, COL_ID, &md_id,
		       COL_USE, &md_use, -1);

    if (md_use != XIFloatingSlave)
    {
        if (use == XISlavePointer && md_use != XIMasterPointer)
            return;
        if (use == XISlaveKeyboard && md_use != XIMasterKeyboard)
            return;
    }

    switch(pos)
    {
        case GTK_TREE_VIEW_DROP_BEFORE:
        case GTK_TREE_VIEW_DROP_INTO_OR_BEFORE:
            gtk_tree_store_insert_before(GTK_TREE_STORE(model),
                                         &ins_iter, final_parent, final_sib);
            break;
        case GTK_TREE_VIEW_DROP_AFTER:
        case GTK_TREE_VIEW_DROP_INTO_OR_AFTER:
            gtk_tree_store_insert_after(GTK_TREE_STORE(model),
                                         &ins_iter, final_parent, final_sib);
            break;
    }
    /* add to new row, remove from old row */
    gtk_tree_store_set(GTK_TREE_STORE(model), &ins_iter,
            COL_ID, id, COL_NAME, name, COL_USE, use, -1);
    gtk_tree_store_remove(GTK_TREE_STORE(model), &sel_iter);
    g_free(name);
    g_free(md_name);

    /* add to changes list */
    gds->changes = g_list_append(gds->changes, change_attachment(gds, id, md_id));
    toggle_cancelapply_buttons(gds, TRUE);
}

/**
 * Apply button clicked.
 * Gather all the changes in the list and apply them to the display.
 */
static void apply(GDeviceSetup *gds)
{
    int num_changes = 0;
    XIAnyHierarchyChangeInfo *all_changes;
    GList *it;
    int i;

    /* count number of elements in list first */
    it = gds->changes;
    while(it)
    {
        num_changes++;
        it = g_list_next(it);
    }

    if (!num_changes)
        return;

    /* push everything from list into array, free list, then apply to dpy */
    all_changes = malloc(num_changes * sizeof(XIAnyHierarchyChangeInfo));
    if (!all_changes)
    {
        g_debug("No memory.");
        return;
    }

    for (i = 0, it = gds->changes;
         i < num_changes && it;
         i++, it = g_list_next(it))
    {
        all_changes[i] = *((XIAnyHierarchyChangeInfo*)it->data);
        free(it->data);
    }

    XIChangeHierarchy(gds->dpy, all_changes, num_changes);
    XFlush(gds->dpy);

    g_list_free(gds->changes);
    gds->changes = NULL;
    query_devices(gds); /* update display */
}

/**
 * New master device button clicked.
 * Open up a dialog to prompt for the name, create the device on "ok".
 */
static void signal_new_md(GtkWidget *widget,
                          gpointer data)
{
    GDeviceSetup *gds;
    GtkDialog *popup;
    GtkWidget *entry,
              *label,
              *hbox;
    gint response;
    const gchar *name;

    gds = (GDeviceSetup*)data;

    popup = (GtkDialog*)gtk_dialog_new();
    gtk_container_set_border_width(GTK_CONTAINER(popup), 10);
    gtk_window_set_modal(GTK_WINDOW(popup), TRUE);
    entry = gtk_entry_new_with_max_length(50);

    label = gtk_label_new("Device Name:");
    hbox = gtk_hbox_new(FALSE, 0);

    gtk_box_pack_start(GTK_BOX(hbox), label, TRUE, FALSE, 10);
    gtk_box_pack_end(GTK_BOX(hbox), entry, TRUE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(popup->vbox), hbox, TRUE, FALSE, 0);

    gtk_dialog_add_button(popup, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(popup, GTK_STOCK_OK, GTK_RESPONSE_OK);
    gtk_dialog_set_default_response(popup, GTK_RESPONSE_OK);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);

    gtk_widget_show_all(GTK_WIDGET(popup));
    response = gtk_dialog_run(popup);

    if (response == GTK_RESPONSE_OK)
    {
        name = gtk_entry_get_text(GTK_ENTRY(entry));
        create_master(gds, name);

        query_devices(gds); /* update view */
    }

    gtk_widget_hide(GTK_WIDGET(popup));
    gtk_widget_destroy(GTK_WIDGET(popup));

}

/**
 * Popup menu item has been clicked. This requires removing a master device.
 */
static gboolean signal_popup_activate(GtkWidget *menuitem, gpointer data)
{
    GDeviceSetup *gds = (GDeviceSetup*)data;
    GtkTreeView *treeview = GTK_TREE_VIEW(gds->treeview);
    GtkTreeModel *model = gtk_tree_view_get_model(treeview);
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    int id;

    selection = gtk_tree_view_get_selection(treeview);
    gtk_tree_selection_get_selected(selection, NULL, &iter);

    gtk_tree_model_get(model, &iter, COL_ID, &id, -1);

    remove_master(gds, id);

    query_devices(gds);
    return TRUE;
}

/**
 * A button has been clicked. If it was the right mouse button, display a
 * popup menu.
 */
static gboolean signal_button_press(GtkTreeView *treeview,
                                    GdkEventButton *event,
                                    gpointer data)
{
    GDeviceSetup *gds;
    GtkWidget *menu, *menuitem;
    GtkTreePath *path;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    GtkTreeModel *model;
    int use, id;

    gds = (GDeviceSetup*)data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3)
    {
        selection = gtk_tree_view_get_selection(treeview);
        if (gtk_tree_view_get_path_at_pos(treeview, event->x, event->y, &path,
                                          NULL, NULL, NULL))
        {
            gtk_tree_selection_select_path(selection, path);
            model = gtk_tree_view_get_model(treeview);

            gtk_tree_model_get_iter(GTK_TREE_MODEL(model), &iter, path);
            gtk_tree_model_get(GTK_TREE_MODEL(model), &iter,
                               COL_ID, &id, COL_USE, &use, -1);

            if (use == XIMasterPointer || use == XIMasterKeyboard || use == XIFloatingSlave)
            {
                menu = gtk_menu_new();
                menuitem = gtk_menu_item_new_with_label("Remove");

                if (id < 2 || use == XIFloatingSlave) /* VCP or VCK*/
                    gtk_widget_set_sensitive(menuitem, FALSE);

                g_signal_connect(menuitem, "activate",
                                 G_CALLBACK(signal_popup_activate), gds);
                gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
                gtk_widget_show_all(menu);
                gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
                        event->button, gdk_event_get_time((GdkEvent*)event));
            }

            gtk_tree_path_free(path);
        }
        return TRUE;
    }
    return FALSE;
}


static GdkPixbuf* load_icon(int what)
{
    GtkIconTheme *icon_theme;
    GdkPixbuf *pixbuf;
    GError *error = NULL;
    char* icon;

    icon_theme = gtk_icon_theme_get_default();

    switch(what)
    {
        case ICON_MOUSE: icon = "mouse"; break;
        case ICON_KEYBOARD: icon = "keyboard"; break;
        case ICON_FLOATING: icon = "dialog-warning"; break; /* XXX */
    }

    pixbuf = gtk_icon_theme_load_icon(icon_theme, icon,16, 0, &error);

    if (!pixbuf)
    {
        g_debug("Couldn't load icon: %s", error->message);
        g_error_free(error);
    }

    return pixbuf;
}

/**
 * Build data storage by querying the X server for all input devices.
 * Can be called multiple times, in which case it'll clean out and re-fill
 * update the tree store.
 */
static GtkTreeStore* query_devices(GDeviceSetup* gds)
{
    GtkTreeStore *treestore;
    GtkTreeModel *model;
    GtkTreeIter iter, child;
    XIDeviceInfo *devices, *dev;
    int ndevices;
    int i, j;
    int icontype;
    GdkPixbuf *icon;
    int valid, child_valid;
    int id, masterid;

    if (!gds->treeview)
    {
        treestore = gtk_tree_store_new(NUM_COLS,
                                       G_TYPE_UINT, /* deviceid*/
                                       G_TYPE_STRING, /* name */
                                       G_TYPE_UINT,
                                       GDK_TYPE_PIXBUF,
                                       G_TYPE_UINT
                                       );
        model = GTK_TREE_MODEL(treestore);
    } else
    {
        model = gtk_tree_view_get_model(gds->treeview);
        treestore = GTK_TREE_STORE(model);
    }

    gds->generation++;
    devices = XIQueryDevice(gds->dpy, XIAllDevices, &ndevices);

    /* First, run through all master device and append them to the tree store
     */
    for (i = 0; i < ndevices; i++)
    {
        dev = &devices[i];

        if (dev->use != XIMasterPointer && dev->use != XIMasterKeyboard)
            continue;

        valid = gtk_tree_model_get_iter_first(model, &iter);
        g_debug("MD: %s", devices[i].name);

        while(valid) {
            gtk_tree_model_get(model, &iter, COL_ID, &id, -1);
            if (id == dev->deviceid)
            {
                gtk_tree_store_set(treestore, &iter,
                                   COL_GENERATION, gds->generation, -1);
                valid = 0xFF;
                break;
            }
            valid = gtk_tree_model_iter_next(model, &iter);
        }

        if (valid != 0xFF) /* new MD */
        {
            icontype = (dev->use == XIMasterPointer) ? ICON_MOUSE : ICON_KEYBOARD;
            icon = load_icon(icontype);

            gtk_tree_store_append(treestore, &iter, NULL);
            gtk_tree_store_set(treestore, &iter,
                               COL_ID, dev->deviceid,
                               COL_NAME, dev->name,
                               COL_USE, dev->use,
                               COL_ICON, icon,
                               COL_GENERATION, gds->generation,
                               -1);
            g_object_unref(icon);
        }
    }

    /* search for Floating fake device */
    valid = gtk_tree_model_get_iter_first(model, &iter);
    while(valid)
    {
        gtk_tree_model_get(model, &iter, COL_ID, &id, -1);
        if (id == XIFloatingSlave)
            break;

        valid = gtk_tree_model_iter_next(model, &iter);
    }

    if (!valid)
    {
        /* Attach a fake device for "Floating" */
        icon = load_icon(ICON_FLOATING);
        gtk_tree_store_append(treestore, &iter, NULL);
        gtk_tree_store_set(treestore, &iter,
                COL_ID, XIFloatingSlave,
                COL_NAME, "Floating",
                COL_USE, XIFloatingSlave,
                COL_ICON, icon,
                COL_GENERATION, gds->generation,
                -1);
        g_object_unref(icon);
    } else {
        GtkTreeIter prev;
        GtkTreeIter pos = iter; /* current position of Floating */

        /* always move Floating fake device to end of list */
        while(valid)
        {
            prev = iter;
            valid = gtk_tree_model_iter_next(model, &iter);
        }

        gtk_tree_store_move_after(treestore, &pos, &prev);

        /* update generation too */
        gtk_tree_store_set(treestore, &pos,
                           COL_GENERATION, gds->generation, -1);
    }


    /* now that we added all MDs, run through again and add SDs to the
     * respective MD */
    for (i = 0; i < ndevices; i++)
    {
        dev = &devices[i];

        if (dev->use == XIMasterPointer || dev->use == XIMasterKeyboard)
   	  continue;

        g_debug("SD: %s", dev->name);

	valid = gtk_tree_model_get_iter_first(model, &iter);
	while(valid) {
	  gtk_tree_model_get(model, &iter, COL_ID, &masterid, -1);
	  if(masterid == dev->deviceid)
	    {
	      /* found master, check if we're already attached to it in
	       * the tree model */
	      child_valid = gtk_tree_model_iter_children(model, &child, &iter);
	      while (child_valid)
		{
		  gtk_tree_model_get(model, &child, COL_ID, &id);

		  if (id == dev->deviceid)
		    {
		      gtk_tree_store_set(treestore, &child,
					 COL_GENERATION, gds->generation, -1);
		      child_valid = 0xFF;
		      break;
		    }

		  child_valid = gtk_tree_model_iter_next(model, &child);
		}

	      /* new device, attach */
	      if (child_valid != 0xFF)
		{
		  gtk_tree_store_append(treestore, &child, &iter);
		  gtk_tree_store_set(treestore, &child,
				     COL_ID, dev->deviceid,
				     COL_NAME, dev->name,
				     COL_USE, dev->use,
				     COL_GENERATION, gds->generation,
				     -1);
		}
	      break;
	    }

	  valid = gtk_tree_model_iter_next(model, &iter);
	}
	break;
        
    }

    XIFreeDeviceInfo(devices);

    /* clean tree store of anything that doesn't have the current
       server generation */

    valid = gtk_tree_model_get_iter_first(model, &iter);
    while(valid)
    {
        int gen;

        child_valid = gtk_tree_model_iter_children(model, &child, &iter);
        while(child_valid)
        {
            gtk_tree_model_get(model, &child, COL_GENERATION, &gen, -1);
            if (gen < gds->generation)
                child_valid = gtk_tree_store_remove(treestore, &child);
            else
                child_valid = gtk_tree_model_iter_next(model, &child);
        }

        gtk_tree_model_get(model, &iter, COL_GENERATION, &gen, -1);
        if (gen < gds->generation)
            valid = gtk_tree_store_remove(treestore, &iter);
        else
            valid = gtk_tree_model_iter_next(model, &iter);
    }

    return treestore;
}


/**
 * Assemble the list view.
 */
static GtkTreeView* get_tree_view(GDeviceSetup *gds)
{
    GtkTreeStore *ts = query_devices(gds);
    GtkTreeView  *tv;
    GtkTreeViewColumn *col;
    GtkCellRenderer *renderer;
    GtkTargetEntry dnd_targets[] = {{"DEV_LIST", GTK_TARGET_SAME_WIDGET, 0xFF}};
    int dnd_ntargets = sizeof(dnd_targets)/sizeof(GtkTargetEntry);

    tv = (GtkTreeView*)gtk_tree_view_new();
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, "Name");
    gtk_tree_view_append_column(tv, col);

    renderer = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, renderer, FALSE);
    gtk_tree_view_column_set_attributes(col, renderer, "pixbuf", COL_ICON, NULL);

    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, renderer, TRUE);
    gtk_tree_view_column_add_attribute(col, renderer, "text", COL_NAME);

    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(ts));
    g_object_unref(ts);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(tv),
                                GTK_SELECTION_SINGLE);

    gtk_tree_view_enable_model_drag_source(tv,
                                           GDK_BUTTON1_MASK,
                                           dnd_targets,
                                           dnd_ntargets,
                                           GDK_ACTION_MOVE);
    gtk_tree_view_enable_model_drag_dest(tv,
                                         dnd_targets,
                                         dnd_ntargets,
                                         GDK_ACTION_MOVE);
    gtk_tree_view_expand_all(tv);
    g_signal_connect(tv, "drag_data_received",
                     G_CALLBACK(signal_dnd_recv), gds);
    g_signal_connect(tv, "button-press-event",
                     G_CALLBACK(signal_button_press), gds);

    return tv;
}


int main (int argc, char *argv[])
{
    GDeviceSetup gds = { NULL, NULL, NULL, NULL, 0};
    GtkWidget *window;
    GtkWidget *bt_new;
    GtkWidget *icon;
    GtkWidget *message;
    int response;
    int loop = TRUE;

    gds.dpy = dpy_init();
    if (!gds.dpy)
    {
        fprintf(stderr, "Cannot connect to X server, or X server does not "
                        "support XI 2.");
        return 1;
    }
    gtk_init(&argc, &argv);

    /* init dialog window */
    window = gtk_dialog_new();
    gds.window = window;
    gtk_container_set_border_width(GTK_CONTAINER(window), 10);

    gtk_dialog_add_buttons(GTK_DIALOG(window),
                           GTK_STOCK_HELP, GTK_RESPONSE_HELP,
                           GTK_STOCK_APPLY, GTK_RESPONSE_APPLY,
                           GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                           GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                           NULL);
    toggle_cancelapply_buttons(&gds, FALSE);

    /* main dialog area */
    gds.treeview = get_tree_view(&gds);

    bt_new = gtk_button_new_with_mnemonic("_Create Cursor/Keyboard Focus");
    icon   = gtk_image_new_from_stock(GTK_STOCK_ADD, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(bt_new), icon);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox),
                       GTK_WIDGET(gds.treeview), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(window)->vbox), bt_new, 0, 0, 10);
    g_signal_connect(G_OBJECT(bt_new), "clicked",
                     G_CALLBACK(signal_new_md), &gds);


    gtk_widget_show_all(window);

    do {
        response = gtk_dialog_run(GTK_DIALOG(window));
        switch(response)
        {
            case GTK_RESPONSE_HELP:
                break;
            case GTK_RESPONSE_CANCEL:
                query_devices(&gds);
                g_list_free(gds.changes);
                gds.changes = NULL;
                toggle_cancelapply_buttons(&gds, FALSE);
                break;
            case GTK_RESPONSE_CLOSE:
                if (gds.changes)
                {
                    message = gtk_message_dialog_new(GTK_WINDOW(window),
                                                     GTK_DIALOG_MODAL,
                                                     GTK_MESSAGE_QUESTION,
                                                     GTK_BUTTONS_YES_NO,
                                                     "You have unapplied "
                                                     "changes. Are you sure "
                                                     "you want to quit?");
                    response = gtk_dialog_run(GTK_DIALOG(message));
                    gtk_widget_destroy(message);
                    loop = (response == GTK_RESPONSE_NO);
                } else
                    loop = FALSE;
                break;
            case GTK_RESPONSE_APPLY:
                apply(&gds);
                toggle_cancelapply_buttons(&gds, FALSE);
                break;
            case GTK_RESPONSE_DELETE_EVENT:
                loop = FALSE;
                break;
        }
    } while (loop);

    g_list_free(gds.changes);

    XCloseDisplay(gds.dpy);

    return 0;
}
