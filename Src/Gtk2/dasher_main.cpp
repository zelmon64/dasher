#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gtk/gtk.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "dasher.h"
#ifdef WITH_MAEMO
#include "dasher_maemo_helper.h"
#endif
#include "dasher_main.h"
#include "GtkDasherControl.h"
#include "Menu.cc"

struct _DasherMainPrivate {
  GladeXML *pGladeXML;
  DasherAppSettings *pAppSettings;

  // Various widgets which need to be cached:
  GtkWidget *pActionPane;
  GtkWidget *pBufferView;
  GtkWidget *pDivider;
  GtkWidget *pEditPane;
  GtkWidget *pMainWindow;
  GtkWidget *pToolbar;
#ifdef WITH_MAEMO
  DasherMaemoHelper *pMaemoHelper;
#endif

  // Properties of the window
  bool bShowEdit;
  bool bShowActions;
  bool bTopMost;
  bool bFullScreen;

  int iWidth;
  int iHeight;
};

typedef struct _DasherMainPrivate DasherMainPrivate;

// Private member functions

static void dasher_main_class_init(DasherMainClass *pClass);
static void dasher_main_init(DasherMain *pMain);
static void dasher_main_destroy(GObject *pObject);
static void dasher_main_refresh_font(DasherMain *pSelf);
static GtkWidget *dasher_main_create_dasher_control(DasherMain *pSelf);
static void dasher_main_on_map(DasherMain *pSelf);
static void dasher_main_setup_window_position(DasherMain *pSelf);
static void dasher_main_setup_window_style(DasherMain *pSelf, bool bTopMost);

// Private functions not in class
extern "C" gboolean take_real_focus(GtkWidget *widget, GdkEventFocus *event, gpointer user_data);
extern "C" gboolean edit_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
extern "C" gboolean edit_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data);

GType dasher_main_get_type() {

  static GType dasher_main_type = 0;

  if(!dasher_main_type) {
    static const GTypeInfo dasher_main_info = {
     sizeof(DasherMainClass),
      NULL,
      NULL,
      (GClassInitFunc) dasher_main_class_init,
      NULL,
      NULL,
      sizeof(DasherMain),
      0,
      (GInstanceInitFunc) dasher_main_init,
      NULL
    };

    dasher_main_type = g_type_register_static(G_TYPE_OBJECT, "DasherMain", &dasher_main_info, static_cast < GTypeFlags > (0));
  }

  return dasher_main_type;
}

static void dasher_main_class_init(DasherMainClass *pClass) {
  GObjectClass *pObjectClass = (GObjectClass *) pClass;
  pObjectClass->finalize = dasher_main_destroy;
}

static void dasher_main_init(DasherMain *pDasherControl) {
  pDasherControl->private_data = new DasherMainPrivate;
  
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pDasherControl->private_data);
  pPrivate->pAppSettings = 0;

  dasher_main_load_interface(pDasherControl);
  dasher_main_setup_window(pDasherControl);
}

static void dasher_main_destroy(GObject *pObject) {
  // FIXME - I think we need to chain up through the finalize methods
  // of the parent classes here...
}

// Public methods

DasherMain *dasher_main_new() {
  DasherMain *pDasherControl;
  pDasherControl = (DasherMain *)(g_object_new(dasher_main_get_type(), NULL));

  return pDasherControl;
}

void dasher_main_load_interface(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);

  const char *szGladeFilename;

#ifdef WITH_GPE
  szGladeFilename = PROGDATA "/dashergpe.glade";
#elif WITH_MAEMO
  szGladeFilename = "/var/lib/install" PROGDATA "/dashermaemo.glade";
  //xml = glade_xml_new(PROGDATA "/dashermaemo.glade", NULL, NULL);
#else
  szGladeFilename = PROGDATA "/dasher.glade";
#endif

  pPrivate->pGladeXML = glade_xml_new(PROGDATA "/dasher.glade", NULL, NULL);

  if (!pPrivate->pGladeXML) {
    g_error("Can't find Glade file: %s. Dasher is unlikely to be correctly installed.", szGladeFilename);
  }

  glade_xml_signal_autoconnect(pPrivate->pGladeXML);

  // Save the details of some of the widgets for later
  pPrivate->pActionPane = glade_xml_get_widget(pPrivate->pGladeXML, "vbox39");
  pPrivate->pBufferView = glade_xml_get_widget(pPrivate->pGladeXML, "the_text_view");
  pPrivate->pDivider = glade_xml_get_widget(pPrivate->pGladeXML, "hpaned1");
  pPrivate->pEditPane = glade_xml_get_widget(pPrivate->pGladeXML, "vbox40");
  pPrivate->pMainWindow = glade_xml_get_widget(pPrivate->pGladeXML, "window");
  pPrivate->pToolbar = glade_xml_get_widget(pPrivate->pGladeXML, "toolbar");

  // TODO: Specify callbacks in glade file
  // TODO: Rationalise focus
  g_signal_connect(G_OBJECT(pPrivate->pBufferView), "button-release-event", G_CALLBACK(take_real_focus), NULL);
  g_signal_connect(G_OBJECT(pPrivate->pBufferView), "key-press-event", G_CALLBACK(edit_key_press), NULL);
  g_signal_connect(G_OBJECT(pPrivate->pBufferView), "key-release-event", G_CALLBACK(edit_key_release), NULL);

  // Create a Maemo helper if necessary
#ifdef WITH_MAEMO
  pPrivate->pMaemoHelper = dasher_maemo_helper_new(pPrivate->pBufferView);
#endif

  // Set up any non-registry-dependent options
#ifdef WITH_GPE
  gtk_window_set_decorated(GTK_WINDOW(pPrivate->pMainWindow), false);
#endif
}

void dasher_main_setup_window(DasherMain *pSelf) {
}


void dasher_main_handle_pre_parameter_change(DasherMain *pSelf, int iParameter) {
  switch( iParameter ) {
  case APP_LP_STYLE:
    dasher_main_save_state(pSelf);
    break;
  }
}

void dasher_main_handle_parameter_change(DasherMain *pSelf, int iParameter) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);

  switch( iParameter ) {
  case APP_BP_SHOW_TOOLBAR:
    if( dasher_app_settings_get_bool(pPrivate->pAppSettings, APP_BP_SHOW_TOOLBAR))
      gtk_widget_show(pPrivate->pToolbar);
    else
      gtk_widget_hide(pPrivate->pToolbar);
    break;
  case APP_SP_EDIT_FONT:
    dasher_main_refresh_font(pSelf);
    break;
  case APP_LP_STYLE:
    dasher_main_on_map(pSelf);
    break;
  }

}

GladeXML *dasher_main_get_glade(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);
  return pPrivate->pGladeXML;
}

GtkWidget *dasher_main_get_window(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);
  return pPrivate->pMainWindow;
}

void dasher_main_set_app_settings(DasherMain *pSelf, DasherAppSettings *pAppSettings) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);
  pPrivate->pAppSettings = pAppSettings;

  // Now we have access to the settings, we can set up the intial
  // values
  
#ifndef WITH_MAEMO
  // TODO: bring into object framework
  PopulateMenus(pPrivate->pGladeXML);


  if(dasher_app_settings_get_bool(pPrivate->pAppSettings, APP_BP_SHOW_TOOLBAR)) {
    gtk_widget_show(pPrivate->pToolbar);
  }

  dasher_main_load_state(pSelf);
#endif

  dasher_main_refresh_font(pSelf);
}

void dasher_main_load_state(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);

  int iWindowWidth;
  int iWindowHeight;
  int iEditHeight;
  
  if(dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_STYLE) != 1) {
    iEditHeight = dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_EDIT_HEIGHT);
    iWindowWidth = dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_SCREEN_WIDTH);
    iWindowHeight = dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_SCREEN_HEIGHT);
  }
  else {
    iEditHeight = dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_EDIT_WIDTH);
    iWindowWidth = dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_SCREEN_WIDTH_H);
    iWindowHeight = dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_SCREEN_HEIGHT_H);
  }

  gtk_window_resize(GTK_WINDOW(pPrivate->pMainWindow), iWindowWidth, iWindowHeight);
  gtk_paned_set_position(GTK_PANED(pPrivate->pDivider), iEditHeight);

  pPrivate->iWidth = iWindowWidth;
  pPrivate->iHeight = iWindowHeight;
}

void dasher_main_save_state(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);

   int iWindowWidth;
   int iWindowHeight;
   int iEditHeight;

   gtk_window_get_size(GTK_WINDOW(pPrivate->pMainWindow), &iWindowWidth, &iWindowHeight);
   iEditHeight = gtk_paned_get_position(GTK_PANED(pPrivate->pDivider));

   if(dasher_app_settings_get_long(pPrivate->pAppSettings, APP_LP_STYLE) != 1) {
     dasher_app_settings_set_long(pPrivate->pAppSettings, APP_LP_EDIT_HEIGHT, iEditHeight);
     dasher_app_settings_set_long(pPrivate->pAppSettings, APP_LP_SCREEN_WIDTH, iWindowWidth);
     dasher_app_settings_set_long(pPrivate->pAppSettings, APP_LP_SCREEN_HEIGHT, iWindowHeight);
   }
   else {
     dasher_app_settings_set_long(pPrivate->pAppSettings, APP_LP_EDIT_WIDTH, iEditHeight);
     dasher_app_settings_set_long(pPrivate->pAppSettings, APP_LP_SCREEN_WIDTH_H, iWindowWidth);
     dasher_app_settings_set_long(pPrivate->pAppSettings, APP_LP_SCREEN_HEIGHT_H, iWindowHeight);
   } 
}

void dasher_main_refresh_font(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);

  const gchar *szFontName = dasher_app_settings_get_string(pPrivate->pAppSettings, APP_SP_EDIT_FONT);
  
  if(!strcmp(szFontName, "")) {
    gtk_widget_modify_font(pPrivate->pBufferView, pango_font_description_from_string(szFontName));
  }
}

void dasher_main_show(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);
  gtk_widget_show(pPrivate->pMainWindow);
}

GtkWidget *dasher_main_create_dasher_control(DasherMain *pSelf) {
  GtkWidget *pDasherControl = gtk_dasher_control_new();

#ifdef WITH_MAEMO
  gtk_widget_set_size_request(pDasherControl, 175, -1);  
#endif

  return pDasherControl;
}


// TODO: Rationalise window setup functions
void dasher_main_setup_window_position(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);
  
//   if(!GTK_WIDGET_MAPPED(window))
//     return;

  // FIXME - what does gravity actually achieve here?

  //   gint iWidth;
//   gint iHeight;

//   

//   gtk_window_resize(GTK_WINDOW(window),(int)round(gdk_screen_width() * g_dXFraction), (int)round(gdk_screen_height() * g_dYFraction) );

//   gtk_window_get_size(GTK_WINDOW(window), &iWidth, &iHeight);
//   gtk_window_move(GTK_WINDOW(window), gdk_screen_width() - iWidth - 32,  gdk_screen_height() - iHeight - 32);

//   gtk_window_get_size(GTK_WINDOW(g_pHiddenWindow), &iWidth, &iHeight);
//   gtk_window_set_gravity(GTK_WINDOW(g_pHiddenWindow), GDK_GRAVITY_SOUTH_EAST);
//   gtk_window_move(GTK_WINDOW(g_pHiddenWindow), gdk_screen_width() - iWidth - 32,  gdk_screen_height() - iHeight - 32);


  // This is neat, but needs to be thought through


  GdkRectangle sFrameRect;
  gdk_window_get_frame_extents(GDK_WINDOW(pPrivate->pMainWindow->window), &sFrameRect);

//   int iTargetWidth = pPrivate->iWidth;
//   int iTargetHeight = pPrivate->iHeight;

  int iTargetWidth = sFrameRect.width;
  int iTargetHeight = sFrameRect.height;
  

  int iScreenWidth = gdk_screen_width();
  int iScreenHeight = gdk_screen_height();
  int iScreenTop;
  int iLeft;
  int iTop;
  int iBuffer = 4;


//   Atom atom_strut_partial = gdk_x11_get_xatom_by_name("_NET_WM_STRUT_PARTIAL");
   guint32 struts[12] = {0, 0, 0, 0, 0, 0 ,0 ,0 ,0 ,0 ,0 ,0};

//   XChangeProperty(GDK_WINDOW_XDISPLAY(window->window),
//   		  GDK_WINDOW_XWINDOW(window->window),
//   		  atom_strut_partial,
//   		  XA_CARDINAL, 32, PropModeReplace,
//   		  (guchar *)&struts, 12);

  int iDockPosition(dasher_app_settings_get_long(g_pDasherAppSettings, APP_LP_DOCK_STYLE));

  g_message("Dock position: %d", iDockPosition);
  if(iDockPosition < 4) {

    //  gtk_window_get_size(GTK_WINDOW(window), &iTargetWidth, &iTargetHeight);

  // TODO: Need full struts for old window managers

  Atom atom_work_area = gdk_x11_get_xatom_by_name("_NET_WORKAREA");
  Atom aReturn;
  int iFormatReturn;
  unsigned long iItemsReturn;
  unsigned long iBytesAfterReturn;
  unsigned char *iData;

  XGetWindowProperty(GDK_WINDOW_XDISPLAY(GDK_ROOT_PARENT()),
		     GDK_WINDOW_XWINDOW(GDK_ROOT_PARENT()),
		     atom_work_area,
		     0, 4, false,
		     XA_CARDINAL, 
		     &aReturn,
		     &iFormatReturn,
		     &iItemsReturn,
		     &iBytesAfterReturn,
		     &iData);

  // TODO: need to use width here
  // TODO: need more error checking with raw X11 stuff
  
  iScreenTop = ((unsigned long *)iData)[1];
  iScreenHeight = ((unsigned long *)iData)[3];

  XFree(iData);

  switch(iDockPosition) {
  case 0: // Top left
    struts[0] = iTargetWidth;
    struts[4] = iScreenTop;
    struts[5] = iTargetHeight + iScreenTop;

    iLeft = iBuffer;
    iTop = iScreenTop + iBuffer;

    gtk_window_set_gravity(GTK_WINDOW(pPrivate->pMainWindow), GDK_GRAVITY_NORTH_WEST);

    break;
  case 1: // Top right
    struts[1] = iTargetWidth;
    struts[6] = iScreenTop;
    struts[7] = iTargetHeight + iScreenTop;

    iLeft = iScreenWidth - iTargetWidth - iBuffer;
    iTop = iScreenTop + iBuffer;

    gtk_window_set_gravity(GTK_WINDOW(pPrivate->pMainWindow), GDK_GRAVITY_NORTH_EAST);

    break;
  case 2: // Bottom left
    struts[0] = iTargetWidth;
    struts[4] = iScreenHeight + iScreenTop - iTargetHeight;
    struts[5] = iScreenHeight + iScreenTop;

    iLeft = iBuffer;
    iTop =  iScreenHeight + iScreenTop - iTargetHeight - iBuffer;

    gtk_window_set_gravity(GTK_WINDOW(pPrivate->pMainWindow), GDK_GRAVITY_SOUTH_WEST);

    break;
  case 3: // Bottom right
    struts[1] = iTargetWidth;
    struts[6] = iScreenHeight + iScreenTop - iTargetHeight;
    struts[7] = iScreenHeight + iScreenTop;

    iLeft = iScreenWidth - iTargetWidth - iBuffer;
    iTop = iScreenHeight + iScreenTop - iTargetHeight - iBuffer;

    gtk_window_set_gravity(GTK_WINDOW(pPrivate->pMainWindow), GDK_GRAVITY_SOUTH_EAST);

    break;
  }
  
//   XChangeProperty(GDK_WINDOW_XDISPLAY(window->window),
//   		  GDK_WINDOW_XWINDOW(window->window),
//   		  atom_strut_partial,
//   		  XA_CARDINAL, 32, PropModeReplace,
//   		  (guchar *)&struts, 12);
  
//   Atom atom_type[1];
//   atom_type[0] = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE_DOCK");
  
//   Atom atom_window_type = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE");
  
//   XChangeProperty(GDK_WINDOW_XDISPLAY(window->window),
// 		  GDK_WINDOW_XWINDOW(window->window),
// 		  atom_window_type,
// 		  XA_ATOM, 32, PropModeReplace,
// 		  (guchar *)&atom_type, 1);
  
  gdk_window_move((GdkWindow *)window->window, iLeft, iTop);

}
  else if(iDockPosition == 4) {
    Atom atom_type[1];
    atom_type[0] = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE_NORMAL");
    
    Atom atom_window_type = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE");
    
    XChangeProperty(GDK_WINDOW_XDISPLAY(window->window),
		    GDK_WINDOW_XWINDOW(window->window),
		    atom_window_type,
		    XA_ATOM, 32, PropModeReplace,
		    (guchar *)&atom_type, 1);

    gtk_window_unfullscreen(GTK_WINDOW(window));
  }
  else if(iDockPosition == 5) {
    Atom atom_type[1];
    atom_type[0] = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE_NORMAL");
    
    Atom atom_window_type = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE");
    
    XChangeProperty(GDK_WINDOW_XDISPLAY(window->window),
		    GDK_WINDOW_XWINDOW(window->window),
		    atom_window_type,
		    XA_ATOM, 32, PropModeReplace,
		    (guchar *)&atom_type, 1);
    
    
    gtk_window_fullscreen(GTK_WINDOW(window));
  }

}

// TODO: Don't pass topmost etc - store in object
void dasher_main_setup_window_style(DasherMain *pSelf, bool bTopMost) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);
  
  // Stup the global structure
  GtkWidget *pDividerNew;
  
  switch(dasher_app_settings_get_long(g_pDasherAppSettings, APP_LP_STYLE)) {
  case 0: // Classic style
    pDividerNew = gtk_vpaned_new();
    gtk_widget_reparent(pPrivate->pEditPane, pDividerNew);
    gtk_widget_reparent(pDasherWidget, pDividerNew);
    break;
  case 1: // Composition
    pDividerNew = gtk_hpaned_new();
    gtk_widget_reparent(pDasherWidget, pDividerNew);
    gtk_widget_reparent(pPrivate->pEditPane, pDividerNew);
    break;
  case 2: // Direct
    pDividerNew = gtk_hpaned_new();
    gtk_widget_reparent(pDasherWidget, pDividerNew);
    gtk_widget_reparent(pPrivate->pEditPane, pDividerNew);
    break;
  case 3: // Full Screen
    pDividerNew = gtk_vpaned_new();
    gtk_widget_reparent(pPrivate->pEditPane, pDividerNew);
    gtk_widget_reparent(pDasherWidget, pDividerNew);
    break;
  default:
    g_error("Invalid style");
    break;
  }

  GtkWidget *pOldParent = gtk_widget_get_parent(pPrivate->pDivider);
  gtk_widget_destroy(pPrivate->pDivider);
  gtk_box_pack_start(GTK_BOX(pOldParent), pDividerNew, true, true, 0);
  gtk_widget_show(pDividerNew);
  pPrivate->pDivider = pDividerNew;

  dasher_main_load_state(pSelf);

  // Visibility of components

  if(pPrivate->bShowActions) {
    gtk_widget_show(pPrivate->pActionPane);
  }
  else {
    gtk_widget_hide(pPrivate->pActionPane);
  }

  if(pPrivate->bShowEdit) {
    gtk_widget_show(pPrivate->pEditPane);
  }
  else {
    gtk_widget_hide(pPrivate->pEditPane);
}

  if(pPrivate->bFullScreen) {
    gtk_window_fullscreen(GTK_WINDOW(pPrivate->pMainWindow));
  }
  else {
    gtk_window_unfullscreen(GTK_WINDOW(pPrivate->pMainWindow));
  }

  gtk_window_set_keep_above(GTK_WINDOW(pPrivate->pMainWindow), pPrivate->bTopMost);
  gtk_window_set_accept_focus(GTK_WINDOW(pPrivate->pMainWindow), !(pPrivate->bTopMost));
  gtk_window_set_focus_on_map(GTK_WINDOW(pPrivate->pMainWindow), !(pPrivate->bTopMost));
}

void dasher_main_on_map(DasherMain *pSelf) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);


  // Refresh the properties of the window

  switch(dasher_app_settings_get_long(g_pDasherAppSettings, APP_LP_STYLE)) {
  case 0:
    pPrivate->bShowEdit = true;
    pPrivate->bShowActions = false;
    pPrivate->bTopMost = false;
    pPrivate->bFullScreen = false;
    break;
  case 1:
    pPrivate->bShowEdit = true;
    pPrivate->bShowActions = true;
    pPrivate->bTopMost = true;
    pPrivate->bFullScreen = false;
    break;
  case 2:
    pPrivate->bShowEdit = false;
    pPrivate->bShowActions = false;
    pPrivate->bTopMost = true;
    pPrivate->bFullScreen = false;
    break;
  case 3:
    pPrivate->bShowEdit = true;
    pPrivate->bShowActions = false;
    pPrivate->bTopMost = false;
    pPrivate->bFullScreen = true;
    break;

  }


//   if(g_bOnTop)
//     gtk_window_set_keep_above(GTK_WINDOW(window), true);
#ifdef WITH_MAEMO

   Window xThisWindow = GDK_WINDOW_XWINDOW(pWidget->window);
   Atom atom_im_window = gdk_x11_get_xatom_by_name("_HILDON_IM_WINDOW");

   XChangeProperty(GDK_WINDOW_XDISPLAY(pWidget->window),
 		  GDK_WINDOW_XWINDOW(gdk_screen_get_root_window (gdk_screen_get_default ())),
 		  atom_im_window,
 		  XA_WINDOW, 32, PropModeReplace,
 		  (guchar *)&xThisWindow, 1);
  
  Atom atom_type[1];
  atom_type[0] = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE_INPUT");
  
  Atom atom_window_type = gdk_x11_get_xatom_by_name("_NET_WM_WINDOW_TYPE");
  
  XChangeProperty(GDK_WINDOW_XDISPLAY(pWidget->window),
		  GDK_WINDOW_XWINDOW(pWidget->window),
		  atom_window_type,
		  XA_ATOM, 32, PropModeReplace,
		  (guchar *)&atom_type, 1);

#else
  dasher_main_setup_window_style(pSelf, false);
  dasher_main_setup_window_position(pSelf);
#endif

#ifdef WITH_MAEMO
  dasher_maemo_helper_setup_window(pPrivate->pMainWindow);
#endif
}

void dasher_main_set_filename(DasherMain *pSelf, const gchar *szFilename) {
  DasherMainPrivate *pPrivate = (DasherMainPrivate *)(pSelf->private_data);

  if(szFilename == 0) {
    gtk_window_set_title(GTK_WINDOW(pPrivate->pMainWindow), "Dasher");
  }
  else {
    // TODO: Prepend 'Dasher - ' to filename?
    gtk_window_set_title(GTK_WINDOW(pPrivate->pMainWindow), szFilename);
  }
}

// Callbacks

extern "C" GtkWidget *create_dasher_control(gchar *szName, gchar *szString1, gchar *szString2, gint iInt1, gint iInt2) {
  return dasher_main_create_dasher_control(g_pDasherMain);
}

extern "C" void on_window_map(GtkWidget* pWidget, gpointer pUserData) {
  dasher_main_on_map(g_pDasherMain);
}

// TODO: Incorporate this into class
gboolean g_bForwardKeyboard(false);

gboolean grab_focus() {
  gtk_widget_grab_focus(the_text_view);
  g_bForwardKeyboard = true;
  return true;
}

// TODO: Not really sure what happens here - need to sort out focus behaviour in general
extern "C" bool focus_in_event(GtkWidget *widget, GdkEventFocus *event, gpointer data) {
  return grab_focus();
}

// TODO: Next three handlers should just forward into class
extern "C" gboolean take_real_focus(GtkWidget *widget, GdkEventFocus *event, gpointer user_data) {
  g_bForwardKeyboard = false;
  return false;
}

extern "C" gboolean edit_key_press(GtkWidget *widget, GdkEventKey *event, gpointer user_data) {
  if(g_bForwardKeyboard) {
    gboolean *returnType;
    g_signal_emit_by_name(GTK_OBJECT(pDasherWidget), "key_press_event", event, &returnType);
    return true;
  }
  else {
    return false;
  }
}

extern "C" gboolean edit_key_release(GtkWidget *widget, GdkEventKey *event, gpointer user_data) { 
  if(g_bForwardKeyboard) {
    gboolean *returnType;
    g_signal_emit_by_name(GTK_OBJECT(pDasherWidget), "key_release_event", event, &returnType);
    return true;
  }
  else {
    return false;
  }
}