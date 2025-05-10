#include "stable-gtk-window.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <glib/gspawn.h>

struct _StableGtkWindow
{
  AdwApplicationWindow  parent_instance;

  /* Template widgets */
  GtkHeaderBar        *header_bar;
  GtkPicture          *output_image;
  GtkEntry            *prompt_entry;
  GtkEntry            *negative_prompt_entry;
  GtkSpinButton       *width_spin;
  GtkSpinButton       *height_spin;
  GtkSpinButton       *steps_spin;
  GtkSpinButton       *seed_spin;
  GtkSpinButton       *cfg_scale_spin;
  GtkDropDown         *sampling_method_dropdown;
  GtkButton           *generate_button;
  GtkButton           *stop_button;
  GtkSpinner          *spinner;
  AdwToastOverlay     *toast_overlay;
  GtkButton           *model_chooser;
  GtkBox              *advanced_settings_box;
  GtkToggleButton     *advanced_settings_toggle;
  GtkButton           *initial_image_chooser;
  GtkToggleButton     *img2img_toggle;
  GtkSpinButton       *strength_spin;
  GtkProgressBar      *progress_bar;
  GtkLabel            *status_label;

  /* Generation state */
  GPid                child_pid;
  guint               child_watch_id;
  GCancellable       *cancellable;
  gchar              *output_path;
  gchar              *model_path;
  gchar              *initial_image_path;
  gboolean            is_generating;
};

G_DEFINE_TYPE (StableGtkWindow, stable_gtk_window, ADW_TYPE_APPLICATION_WINDOW)

static void
process_finished_cb (GPid pid, gint status, gpointer user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  
  /* Re-enable UI */
  self->is_generating = FALSE;
  gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->initial_image_chooser), TRUE);
  gtk_widget_set_visible (GTK_WIDGET (self->stop_button), FALSE);
  gtk_spinner_stop (self->spinner);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  
  if (g_cancellable_is_cancelled (self->cancellable)) {
    gtk_label_set_text (self->status_label, "Cancelled");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Generation cancelled"));
  } else if (status == 0) {
    /* Load the generated image */
    GFile *file = g_file_new_for_path (self->output_path);
    gtk_picture_set_file (self->output_image, file);
    g_object_unref (file);
    
    gtk_label_set_text (self->status_label, "Done");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Image generated successfully"));
  } else {
    gtk_label_set_text (self->status_label, "Failed");
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Generation failed"));
  }
  
  g_spawn_close_pid (pid);
  g_object_unref (self->cancellable);
  self->cancellable = NULL;
  self->child_pid = 0;
  self->child_watch_id = 0;
}

static void
model_open_response (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file;
  GError *error = NULL;
  
  file = gtk_file_dialog_open_finish (dialog, result, &error);
  if (file == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to open file: %s", error->message);
    g_error_free (error);
    return;
  }
  
  g_free (self->model_path);
  self->model_path = g_file_get_path (file);
  
  /* Update the button label to show filename */
  char *basename = g_file_get_basename (file);
  char *button_text = g_strdup_printf ("Model: %s", basename);
  gtk_button_set_label (self->model_chooser, button_text);
  g_free (button_text);
  g_free (basename);
  
  g_object_unref (file);
}

static void
on_model_file_select (GtkButton *button,
                     gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  GtkFileDialog *dialog;
  GtkFileFilter *filter;
  GListStore *filters;
  
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Select Stable Diffusion Model");
  
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "Stable Diffusion Models");
  gtk_file_filter_add_pattern (filter, "*.ckpt");
  gtk_file_filter_add_pattern (filter, "*.safetensors");
  gtk_file_filter_add_pattern (filter, "*.gguf");
  
  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  
  gtk_file_dialog_open (dialog,
                       GTK_WINDOW (self),
                       NULL,  /* cancellable */
                       model_open_response,
                       self);
  
  g_object_unref (dialog);
  g_object_unref (filter);
  g_object_unref (filters);
}

static void
image_open_response (GObject *source_object,
                   GAsyncResult *result,
                   gpointer user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  GtkFileDialog *dialog = GTK_FILE_DIALOG (source_object);
  GFile *file;
  GError *error = NULL;
  
  file = gtk_file_dialog_open_finish (dialog, result, &error);
  if (file == NULL) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Failed to open file: %s", error->message);
    g_error_free (error);
    return;
  }
  
  g_free (self->initial_image_path);
  self->initial_image_path = g_file_get_path (file);
  
  /* Update the button label to show filename */
  char *basename = g_file_get_basename (file);
  char *button_text = g_strdup_printf ("Image: %s", basename);
  gtk_button_set_label (self->initial_image_chooser, button_text);
  g_free (button_text);
  g_free (basename);
  
  /* Show the initial image */
  gtk_picture_set_file (self->output_image, file);
  
  g_object_unref (file);
}

static void
on_initial_image_file_select (GtkButton *button,
                             gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  GtkFileDialog *dialog;
  GtkFileFilter *filter;
  GListStore *filters;
  
  dialog = gtk_file_dialog_new ();
  gtk_file_dialog_set_title (dialog, "Select Input Image");
  
  filter = gtk_file_filter_new ();
  gtk_file_filter_set_name (filter, "Images");
  gtk_file_filter_add_mime_type (filter, "image/png");
  gtk_file_filter_add_mime_type (filter, "image/jpeg");
  
  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
  g_list_store_append (filters, filter);
  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));
  
  gtk_file_dialog_open (dialog,
                       GTK_WINDOW (self),
                       NULL,  /* cancellable */
                       image_open_response,
                       self);
  
  g_object_unref (dialog);
  g_object_unref (filter);
  g_object_unref (filters);
}

static void
on_generate_clicked (GtkButton *button,
                     gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  gchar *command_line;
  gchar **argv = NULL;
  GError *error = NULL;
  gint argc;
  
  if (self->is_generating)
    return;
  
  if (self->model_path == NULL) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new ("Please select a model file"));
    return;
  }
  
  self->output_path = g_build_filename (g_get_tmp_dir (), "stable-gtk-output.png", NULL);
  
  /* Prepare the command line */
  if (gtk_toggle_button_get_active (self->img2img_toggle)) {
    if (self->initial_image_path == NULL) {
      adw_toast_overlay_add_toast (self->toast_overlay,
                                 adw_toast_new ("Please select an initial image for img2img"));
      return;
    }
    
    command_line = g_strdup_printf ("sd --mode img2img -m \"%s\" -p \"%s\" -n \"%s\" -W %d -H %d --steps %d --seed %ld --cfg-scale %.1f --sampling-method %s -o \"%s\" -i \"%s\" --strength %.2f",
                                    self->model_path,
                                    gtk_editable_get_text (GTK_EDITABLE (self->prompt_entry)),
                                    gtk_editable_get_text (GTK_EDITABLE (self->negative_prompt_entry)),
                                    (int) gtk_spin_button_get_value (self->width_spin),
                                    (int) gtk_spin_button_get_value (self->height_spin),
                                    (int) gtk_spin_button_get_value (self->steps_spin),
                                    (long) gtk_spin_button_get_value (self->seed_spin),
                                    gtk_spin_button_get_value (self->cfg_scale_spin),
                                    gtk_string_object_get_string (GTK_STRING_OBJECT (
                                      gtk_drop_down_get_selected_item (self->sampling_method_dropdown))),
                                    self->output_path,
                                    self->initial_image_path,
                                    gtk_spin_button_get_value (self->strength_spin));
  } else {
    command_line = g_strdup_printf ("sd -m \"%s\" -p \"%s\" -n \"%s\" -W %d -H %d --steps %d --seed %ld --cfg-scale %.1f --sampling-method %s -o \"%s\"",
                                    self->model_path,
                                    gtk_editable_get_text (GTK_EDITABLE (self->prompt_entry)),
                                    gtk_editable_get_text (GTK_EDITABLE (self->negative_prompt_entry)),
                                    (int) gtk_spin_button_get_value (self->width_spin),
                                    (int) gtk_spin_button_get_value (self->height_spin),
                                    (int) gtk_spin_button_get_value (self->steps_spin),
                                    (long) gtk_spin_button_get_value (self->seed_spin),
                                    gtk_spin_button_get_value (self->cfg_scale_spin),
                                    gtk_string_object_get_string (GTK_STRING_OBJECT (
                                      gtk_drop_down_get_selected_item (self->sampling_method_dropdown))),
                                    self->output_path);
  }
  
  /* Parse the command line */
  if (!g_shell_parse_argv (command_line, &argc, &argv, &error)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new (error->message));
    g_error_free (error);
    g_free (command_line);
    return;
  }
  
  /* Disable UI while generating */
  self->is_generating = TRUE;
  gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->initial_image_chooser), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->stop_button), TRUE);
  gtk_spinner_start (self->spinner);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), TRUE);
  gtk_label_set_text (self->status_label, "Generating...");
  
  /* Start the process */
  self->cancellable = g_cancellable_new ();
  
  if (!g_spawn_async_with_pipes (NULL, argv, NULL, 
                                G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                                NULL, NULL, &self->child_pid, 
                                NULL, NULL, NULL, &error)) {
    adw_toast_overlay_add_toast (self->toast_overlay,
                               adw_toast_new (error->message));
    g_error_free (error);
    g_free (command_line);
    g_strfreev (argv);
    
    /* Re-enable UI */
    self->is_generating = FALSE;
    gtk_widget_set_sensitive (GTK_WIDGET (self->generate_button), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->model_chooser), TRUE);
    gtk_widget_set_sensitive (GTK_WIDGET (self->initial_image_chooser), TRUE);
    gtk_widget_set_visible (GTK_WIDGET (self->stop_button), FALSE);
    gtk_spinner_stop (self->spinner);
    gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
    gtk_label_set_text (self->status_label, "Ready");
    
    return;
  }
  
  g_free (command_line);
  g_strfreev (argv);
  
  /* Monitor the process */
  self->child_watch_id = g_child_watch_add (self->child_pid, 
                                          process_finished_cb, self);
}

static void
on_stop_clicked (GtkButton *button,
                 gpointer   user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  
  if (self->child_pid != 0) {
    g_cancellable_cancel (self->cancellable);
    kill (self->child_pid, SIGTERM);
  }
}

static void
on_img2img_toggled (GtkToggleButton *button,
                    gpointer         user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  gboolean active = gtk_toggle_button_get_active (button);
  
  gtk_widget_set_visible (GTK_WIDGET (self->initial_image_chooser), active);
  gtk_widget_set_visible (GTK_WIDGET (self->strength_spin), active);
  gtk_widget_set_sensitive (GTK_WIDGET (self->strength_spin), active);
}

static void
on_advanced_settings_toggled (GtkToggleButton *button,
                             gpointer         user_data)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (user_data);
  gboolean active = gtk_toggle_button_get_active (button);
  
  gtk_widget_set_visible (GTK_WIDGET (self->advanced_settings_box), active);
}

static void
stable_gtk_window_finalize (GObject *object)
{
  StableGtkWindow *self = STABLE_GTK_WINDOW (object);
  
  if (self->child_pid != 0) {
    kill (self->child_pid, SIGTERM);
    g_spawn_close_pid (self->child_pid);
    g_source_remove (self->child_watch_id);
  }
  
  g_free (self->output_path);
  g_free (self->model_path);
  g_free (self->initial_image_path);
  
  if (self->cancellable != NULL)
    g_object_unref (self->cancellable);
  
  G_OBJECT_CLASS (stable_gtk_window_parent_class)->finalize (object);
}

static void
stable_gtk_window_class_init (StableGtkWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  
  object_class->finalize = stable_gtk_window_finalize;
  
  gtk_widget_class_set_template_from_resource (widget_class, "/com/github/stable-gtk/window.ui");
  
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, output_image);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, prompt_entry);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, negative_prompt_entry);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, width_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, height_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, steps_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, seed_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, cfg_scale_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, sampling_method_dropdown);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, generate_button);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, stop_button);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, spinner);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, model_chooser);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, advanced_settings_box);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, advanced_settings_toggle);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, initial_image_chooser);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, img2img_toggle);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, strength_spin);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, progress_bar);
  gtk_widget_class_bind_template_child (widget_class, StableGtkWindow, status_label);
  
  gtk_widget_class_bind_template_callback (widget_class, on_generate_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_stop_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_model_file_select);
  gtk_widget_class_bind_template_callback (widget_class, on_initial_image_file_select);
  gtk_widget_class_bind_template_callback (widget_class, on_img2img_toggled);
  gtk_widget_class_bind_template_callback (widget_class, on_advanced_settings_toggled);
}

static void
stable_gtk_window_init (StableGtkWindow *self)
{
  GtkStringList *sampling_methods;
  
  gtk_widget_init_template (GTK_WIDGET (self));
  
  self->is_generating = FALSE;
  self->child_pid = 0;
  self->child_watch_id = 0;
  self->cancellable = NULL;
  self->output_path = NULL;
  self->model_path = NULL;
  self->initial_image_path = NULL;
  
  /* Initialize UI values */
  gtk_spin_button_set_value (self->width_spin, 512);
  gtk_spin_button_set_value (self->height_spin, 512);
  gtk_spin_button_set_value (self->steps_spin, 20);
  gtk_spin_button_set_value (self->seed_spin, 42);
  gtk_spin_button_set_value (self->cfg_scale_spin, 7.0);
  gtk_spin_button_set_value (self->strength_spin, 0.75);
  
  /* Set up the sampling method dropdown */
  sampling_methods = gtk_string_list_new (NULL);
  
  gtk_string_list_append (sampling_methods, "euler");
  gtk_string_list_append (sampling_methods, "euler_a");
  gtk_string_list_append (sampling_methods, "heun");
  gtk_string_list_append (sampling_methods, "dpm2");
  gtk_string_list_append (sampling_methods, "dpm++2s_a");
  gtk_string_list_append (sampling_methods, "dpm++2m");
  gtk_string_list_append (sampling_methods, "dpm++2mv2");
  gtk_string_list_append (sampling_methods, "lcm");
  
  gtk_drop_down_set_model (self->sampling_method_dropdown, G_LIST_MODEL (sampling_methods));
  gtk_drop_down_set_selected (self->sampling_method_dropdown, 1); /* Default to euler_a */
  
  /* Hide advanced settings by default */
  gtk_widget_set_visible (GTK_WIDGET (self->advanced_settings_box), FALSE);
  
  /* Hide img2img widgets by default */
  gtk_widget_set_visible (GTK_WIDGET (self->initial_image_chooser), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->strength_spin), FALSE);
  
  /* Hide stop button and spinner by default */
  gtk_widget_set_visible (GTK_WIDGET (self->stop_button), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->spinner), FALSE);
  
  gtk_label_set_text (self->status_label, "Ready");
} 