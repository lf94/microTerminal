#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <pango/pango.h>

#define PROMPT '>'


GtkTextBuffer *text_buffer;
GString *typed_buffer;
int user_key_event;

typedef struct {
	int pipe;
	GtkTextBuffer *text_buffer;
} TextBufferPipe;

typedef struct {
	gchar byte;
	GtkTextBuffer *text_buffer;
} AtomicByteBuffer; 



gboolean insert_text_to_text_buffer(void *param) {
	gchar *byte = (gchar *)param;
	gtk_text_buffer_insert_at_cursor(text_buffer, byte, 1);
	free(byte);
	return FALSE;
}

void * read_shell_output(void *param) {
	TextBufferPipe *tbp = (TextBufferPipe *)param;
	int ui_in_pipe_end = tbp->pipe;
	int characters_read = 0;

	while(1) {
		gchar *byte = malloc(1);
		if(characters_read == 0) {
			gchar *prompt = malloc(1);
			*prompt = PROMPT;
			gdk_threads_add_idle(insert_text_to_text_buffer, prompt);
		}
		// stuck here
		characters_read = read(ui_in_pipe_end, byte, 1);
		gdk_threads_add_idle(insert_text_to_text_buffer, byte);
	}

	return NULL;
}

void insert_text (GtkTextBuffer *textbuffer, GtkTextIter *location, gchar *text, gint len, gpointer user_data) {

	if(!user_key_event) {
		user_key_event = 0;
		return;
	}
	user_key_event = 0;

	TextBufferPipe *tbp = (TextBufferPipe *)user_data;
	int ui_out_pipe_end = tbp->pipe;

	typed_buffer = g_string_append(typed_buffer, text);

	if(g_strcmp0(text, "\n") != 0) {
		return;	
	}

	clearerr(stdin);

	// Send everything typed to stdin
	gsize typed_buffer_len = typed_buffer->len;
	gchar *typed_string = g_string_free(typed_buffer, FALSE);
	write(ui_out_pipe_end, typed_string, typed_buffer_len);

	// free typed buffer and create a new one.
	g_free(typed_string);
	typed_buffer = g_string_new(NULL);

	return;
}

gboolean key_press_event(GtkWidget *text_view, GdkEvent *event, gpointer user_data) {
	user_key_event = 1;

	return FALSE;	
}

int main(int argc, char *argv[]) {
	gtk_init(&argc, &argv);

	/* Initialize global variables. */
	typed_buffer = g_string_new(NULL);

	/* UI initialization and configuration */
	GtkWidget *root_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(
		GTK_SCROLLED_WINDOW(scrolled_window), 
		GTK_POLICY_NEVER, 
		GTK_POLICY_ALWAYS
	);

	GtkWidget *text_view = gtk_text_view_new();
	text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

	gtk_text_view_set_justification(GTK_TEXT_VIEW(text_view), GTK_JUSTIFY_FILL);

	gtk_container_add(GTK_CONTAINER(scrolled_window), text_view);
	gtk_container_add(GTK_CONTAINER(root_window), scrolled_window);

	g_signal_connect(root_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	gtk_widget_grab_focus(text_view);
	gtk_widget_show_all(root_window);

	/* Configure and start initial program */
	int pipe1[2];
	int pipe2[2];
	pipe(pipe1);
	pipe(pipe2);

	if(fork() == 0) {
		close(pipe1[1]);
		close(pipe2[0]);

		dup2(pipe1[0], STDIN_FILENO);
		dup2(pipe2[1], STDOUT_FILENO);

		close(pipe1[0]);
		close(pipe2[1]);

		execl("/bin/sh", "sh", "-s", NULL);
		fprintf(stderr, "FORK DYING\n");
		_exit(EXIT_FAILURE);
	}

	dup2(pipe1[1], STDOUT_FILENO);
	dup2(pipe2[0], STDIN_FILENO);

	close(pipe1[0]);
	close(pipe1[1]);
	close(pipe2[0]);
	close(pipe2[1]);

	user_key_event = 0;

	/* (READ) Pthread thread */
	TextBufferPipe *tbp_out = malloc(sizeof(TextBufferPipe));
	tbp_out->text_buffer = text_buffer;
	tbp_out->pipe = dup(STDIN_FILENO);
	pthread_t read_shell_output_thread;
	pthread_create(&read_shell_output_thread, NULL, read_shell_output, tbp_out);

	/* (WRITE) GTK callback */
	TextBufferPipe *tbp_in = malloc(sizeof(TextBufferPipe));
	tbp_in->text_buffer = text_buffer;
	tbp_in->pipe = dup(STDOUT_FILENO);
	g_signal_connect(text_view, "key-press-event", G_CALLBACK(key_press_event), tbp_in);
	g_signal_connect(text_buffer, "insert-text", G_CALLBACK(insert_text), tbp_in);


	/* Start the chaos. */
	gtk_main();

	/* End the chaos. */
	free(tbp_out);
	free(tbp_in);

	return 1;
}

