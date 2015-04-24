#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <pango/pango.h>


GString *typed_buffer;

typedef struct {
	int pipe;
	GtkTextBuffer *text_buffer;
} TextBufferPipe;

void *read_shell_output(void *param) {
	TextBufferPipe *tbp = (TextBufferPipe *)param;
	GtkTextBuffer *text_buffer = tbp->text_buffer;
	int ui_in_pipe_end = tbp->pipe;
	gchar byte;

	while(1) {
		if(read(ui_in_pipe_end, &byte, 1) == 1) {
			gtk_text_buffer_insert_at_cursor(text_buffer, &byte, 1);
			fprintf(stderr, "READING: %c\n", byte);
		}
	}
	return param;
}

void text_inserted(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint len, gpointer user_data) {
	TextBufferPipe *tbp = (TextBufferPipe *)user_data;
	GtkTextBuffer *text_buffer = tbp->text_buffer;
	int ui_out_pipe_end = tbp->pipe;

	gchar *casefolded_str = g_utf8_casefold(text, len);
	if(g_strcmp0(casefolded_str, "\n") != 0) {
		typed_buffer = g_string_append(typed_buffer, text);
		return;	
	}

	// Send everything typed to stdin
	gsize typed_buffer_len = typed_buffer->len;
	gchar *typed_string = g_string_free(typed_buffer, FALSE);
	write(ui_out_pipe_end, typed_string, typed_buffer_len);

	// free typed buffer and create a new one.
	g_free(typed_string);
	typed_buffer = g_string_new(NULL);

	fprintf(stderr, "WROTE TO OUTPUT\n");
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
	GtkTextBuffer *text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));

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

		execl("/bin/sh", "sh", "-i", NULL);
		_exit(EXIT_FAILURE);
	}

	dup2(pipe1[1], STDOUT_FILENO);
	dup2(pipe2[0], STDIN_FILENO);

	close(pipe1[0]);
	close(pipe1[1]);
	close(pipe2[0]);
	close(pipe2[1]);

	/* GTK callback */
	TextBufferPipe *tbp_in = malloc(sizeof(TextBufferPipe));
	tbp_in->text_buffer = text_buffer;
	tbp_in->pipe = pipe1[1];
	g_signal_connect(text_buffer, "insert-text", G_CALLBACK(text_inserted), tbp_in);

	/* Pthread thread */
	TextBufferPipe *tbp_out = malloc(sizeof(TextBufferPipe));
	tbp_out->text_buffer = text_buffer;
	tbp_out->pipe = pipe2[0];
	pthread_t read_shell_output_thread;
	pthread_create(&read_shell_output_thread, NULL, read_shell_output, tbp_out);

	/* Start the chaos. */
	gtk_main();

	/* End the chaos. */
	pthread_join(read_shell_output_thread, NULL);
	free(tbp_out);
	free(tbp_in);

	return 1;
}

