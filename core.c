/*
 ============================================================================
 Name        : core.c
 Author      : Wadim Mueller
 Version     : 1.0
 Copyright   :
 Description : Simple Disparity calculation of stereo images
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#define DISP_WINDOW_WIDTH 8
#define DISP_WINDOW_HEIGTH 8

#define ARRAY_SIZE(_X_) (sizeof(_X_)/sizeof(_X_[0]))

#define MAX_DISPARTY	25

struct __attribute__ ((__packed__)) rgb_pixels {
	guchar r;
	guchar g;
	guchar b;
};

typedef struct {
	GSList *windows;
} MyApp;

void on_window_destroy(GtkWidget *widget, MyApp *app) {
	app->windows = g_slist_remove(app->windows, widget);

	if (g_slist_length(app->windows) == 0) {
		/* last window was closed... exit */

		g_debug("Exiting...");
		g_slist_free(app->windows);
		gtk_main_quit();
	}
}

static struct rgb_pixels* lut;

static GtkWidget* create_window_for_disparity_map(void) {
	return gtk_window_new(GTK_WINDOW_TOPLEVEL);
}

static GtkWidget* create_new_image_for_disparity(GdkPixbuf* ref) {
	GdkPixbuf * disparity_map;
	disparity_map = gdk_pixbuf_new(gdk_pixbuf_get_colorspace(ref),
			gdk_pixbuf_get_has_alpha(ref), gdk_pixbuf_get_bits_per_sample(ref),
			gdk_pixbuf_get_width(ref), gdk_pixbuf_get_height(ref));

	return gtk_image_new_from_pixbuf(disparity_map);

}

static void display_disparity_map(MyApp *app, GtkWidget *dispWindow,
		GtkWidget *disp_widget) {
	app->windows = g_slist_prepend(app->windows, dispWindow);
	gtk_container_set_border_width(GTK_CONTAINER(dispWindow), 25);
	gtk_container_add(GTK_CONTAINER(dispWindow), disp_widget);
	gchar *titleDisp = g_strdup_printf("Disparity Map %d",
			g_slist_length(app->windows));
	gtk_window_set_title(GTK_WINDOW(dispWindow), titleDisp);
	g_free(titleDisp);

	g_signal_connect(G_OBJECT (dispWindow), "destroy",
			G_CALLBACK (on_window_destroy), app);
	gtk_widget_show_all(dispWindow);
}



static void fetch_window(guint width, guint height, guint image_width,
		struct rgb_pixels* start, struct rgb_pixels* out) {
	for (int i = 0; i < height; i++) {
		for (int j = 0; j < width; ++j) {
			out[i * width + j] = *start;
			start++;
		}
		start += (image_width - width);
	}
}

static guint get_correlation(struct rgb_pixels * ref,
		struct rgb_pixels * cmp, guint size) {
	guint res = 0;
	int tmpr, tmpg, tmpb;
	int rr, gr, br, rc, gc, bc;

	for (int i = 0; i < size; ++i) {
		rr = (int) ref[i].r;
		gr = (int) ref[i].g;
		br = (int) ref[i].b;

		rc = (int) cmp[i].r;
		gc = (int) cmp[i].g;
		bc = (int) cmp[i].b;

		tmpr = abs(rr - rc);
		tmpg = abs(gr - gc);
		tmpb = abs(br - bc);
		res += tmpr + tmpg + tmpb;
	}
	return res;
}

static void getHeatMapColor(float value, float *red, float *green, float *blue)
{
  #define NUM_COLORS  7
  static float color[NUM_COLORS][3] = { {0,0,0.5}, {0,0,1}, {0,1,1},{0,1,0}, {1,1,0}, {1,0,0}, {0.5,0,0}};

  int idx1;
  int idx2;
  float fractBetween = 0;

  if(value <= 0)	      {  idx1 = idx2 = 0;            }
  else if(value >= 1)	  {  idx1 = idx2 = NUM_COLORS-1; }
  else
  {
    value = value * (NUM_COLORS-1);
    idx1  = floor(value);
    idx2  = idx1+1;
    fractBetween = value - (float)idx1;
  }

  *red   = (color[idx2][0] - color[idx1][0])*fractBetween + color[idx1][0];
  *green = (color[idx2][1] - color[idx1][1])*fractBetween + color[idx1][1];
  *blue  = (color[idx2][2] - color[idx1][2])*fractBetween + color[idx1][2];
}

static void calc_color_gradient() {
	float r, g, b;
	lut = (struct rgb_pixels*) malloc(255 * sizeof(struct rgb_pixels));

	for (int i = 0; i <= 255; i++) {
		struct rgb_pixels* entry = &lut[i];
		getHeatMapColor((float) (i) / 255, &r, &g, &b);
		entry->b = (int) (255.0f * b);
		entry->g = (int) (255.0f * g);
		entry->r = (int) (255.0f * r);
	}
}

int calculate_disparity(GdkPixbuf* left, GdkPixbuf* right, MyApp *app) {
	guint lenl, lenr;
	struct rgb_pixels *lrgb;
	struct rgb_pixels *ref;
	struct rgb_pixels *rrgb;
	struct rgb_pixels *cmp;
	struct rgb_pixels *disp_rgb;

	guint img_width = gdk_pixbuf_get_width(left);
	guint img_height = gdk_pixbuf_get_height(left);
	lrgb = (struct rgb_pixels *) gdk_pixbuf_get_pixels_with_length(left, &lenl);
	rrgb = (struct rgb_pixels *) gdk_pixbuf_get_pixels_with_length(right,
			&lenr);

	if (lenl != lenr) {
		printf("Error invalid images\n");
		return -1;
	}
	calc_color_gradient();
	GtkWidget *dispWindow = create_window_for_disparity_map();
	GtkWidget *dispImage = create_new_image_for_disparity(left);
	GdkPixbuf* dispPixBuf = gtk_image_get_pixbuf(dispImage);
	disp_rgb = (struct rgb_pixels *) gdk_pixbuf_get_pixels(dispPixBuf);
	memset(disp_rgb, 0, lenl);

	struct rgb_pixels ref_win[DISP_WINDOW_HEIGTH * DISP_WINDOW_WIDTH];
	struct rgb_pixels cmp_win[DISP_WINDOW_HEIGTH * DISP_WINDOW_WIDTH];

	ref = lrgb;
	cmp = rrgb;

	guint min = UINT_MAX;
	int at_column = 0;

	double time1=0.0, tstart;
	tstart = clock();
	for (int row = 0; row < (img_height - DISP_WINDOW_HEIGTH); ++row) {
		for (int col_left = 0; col_left < (img_width - DISP_WINDOW_WIDTH);
				++col_left) {
#ifdef MAX_DISPARTY
			int pix_cnt = 0;
#endif
			for (int col_right = 0; col_right < (img_width - DISP_WINDOW_WIDTH - col_left);
					++col_right) {
				guint tmp_res;
				fetch_window(DISP_WINDOW_WIDTH, DISP_WINDOW_HEIGTH,
						img_width, cmp, cmp_win);
#ifdef MAX_DISPARTY
				pix_cnt++;
#endif
				cmp++;
				tmp_res = get_correlation(ref_win, cmp_win,
				DISP_WINDOW_WIDTH * DISP_WINDOW_HEIGTH);
				if (tmp_res < min) {
					min = tmp_res;
					at_column = col_right;
//					if (min < 50000)
//						break;
				}
#ifdef MAX_DISPARTY
				if (pix_cnt > MAX_DISPARTY)
					break;
#endif
			}
			cmp = rrgb;
#ifdef MAX_DISPARTY
			if (at_column >= MAX_DISPARTY)
				at_column = 0;
#endif

			disp_rgb->b = lut[at_column * 255/25].b;
			disp_rgb->g = lut[at_column * 255/25].g;
			disp_rgb->r = lut[at_column * 255/25].r;
			min = UINT_MAX;
			at_column = 0;
			ref++;
			cmp += (ref - lrgb);
			fetch_window(DISP_WINDOW_WIDTH, DISP_WINDOW_HEIGTH, img_width, ref, ref_win);
			disp_rgb++;
		}
		ref += DISP_WINDOW_WIDTH;
		cmp += DISP_WINDOW_WIDTH;
		disp_rgb += DISP_WINDOW_WIDTH;
	}
	time1 += clock() - tstart;

	time1 = time1/CLOCKS_PER_SEC;

	printf("Execution Time is %.2lf s\n", time1);

	display_disparity_map(app, dispWindow, dispImage);

	return 0;
}

void create_windows(GtkWidget *widget, MyApp *app) {
	GtkWidget *leftWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	GtkWidget *rightWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gchar *titleLeft;
	gchar *titleRight;

	/* add window to list */

	app->windows = g_slist_prepend(app->windows, leftWindow);
	app->windows = g_slist_prepend(app->windows, rightWindow);

	GtkWidget *leftImage = gtk_image_new_from_file(
			"pics/LEFT.BMP");
	GtkWidget *rightImage = gtk_image_new_from_file(
			"pics/RIGHT.BMP");

	GdkPixbuf *pixBufLeft = gtk_image_get_pixbuf((GtkImage*)leftImage);
	GdkPixbuf *pixBufRight = gtk_image_get_pixbuf((GtkImage*)rightImage);

	calculate_disparity(pixBufLeft, pixBufRight, app);

	gtk_container_set_border_width(GTK_CONTAINER(leftWindow), 25);
	gtk_container_set_border_width(GTK_CONTAINER(rightWindow), 25);
	gtk_container_add(GTK_CONTAINER(leftWindow), leftImage);
	gtk_container_add(GTK_CONTAINER(rightWindow), rightImage);
	titleLeft = g_strdup_printf("Left %d", g_slist_length(app->windows));
	titleRight = g_strdup_printf("Right %d", g_slist_length(app->windows));
	gtk_window_set_title(GTK_WINDOW(leftWindow), titleLeft);
	gtk_window_set_title(GTK_WINDOW(rightWindow), titleRight);
	g_free(titleLeft);
	g_free(titleRight);

	/* connect callbacks to signals */

	g_signal_connect(G_OBJECT (leftWindow), "destroy",
			G_CALLBACK (on_window_destroy), app);
	g_signal_connect(G_OBJECT (rightWindow), "destroy",
			G_CALLBACK (on_window_destroy), app);

	gtk_widget_show_all(leftWindow);
	gtk_widget_show_all(rightWindow);
}

int main(int argc, char *argv[]) {
	MyApp *app;

	gtk_init(&argc, &argv);
	app = g_slice_new(MyApp);
	app->windows = NULL;

	/* create first window */
	create_windows(NULL, app);

	gtk_main();
	g_slice_free(MyApp, app);

	return 0;
}
