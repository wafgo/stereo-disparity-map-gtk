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

struct __attribute__ ((__packed__)) rgb_pixels {
	guchar r;
	guchar g;
	guchar b;
};

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

static guint get_correlation_value(struct rgb_pixels * ref,
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

		tmpr = abs(rr * rr - rc * rc);
		tmpg = abs(gr * gr - gc * gc);
		tmpb = abs(br * br - bc * bc);
		res += tmpr + tmpg + tmpb;
	}
	return res;
}

static struct rgb_pixels lut[25];

void fill_lut( void ) {
	int cnt = ARRAY_SIZE(lut);
	int third = cnt/3;

	for (int i = 0; i < cnt; ++i) {
		struct rgb_pixels* entry = &lut[i];
		if (i < third) {
			entry->g = entry->b = 0;
			entry->r = 255 / third * (i + 1);
		} else if (i >= third && i < (2*third)) {
			entry->r = entry->b = 0;
			entry->g = (255 / (2 * third)) * (i + 1);
		} else {
			entry->r = entry->g = 0;
			entry->b = 255 / cnt * (i + 1);
		}
	}
}
static unsigned char colorGradient[255][3];

static void getHeatMapColor(float value, float *red, float *green, float *blue)
{
  #define NUM_COLORS  7
  static float color[NUM_COLORS][3] = { {0,0,0.5}, {0,0,1}, {0,1,1},{0,1,0}, {1,1,0}, {1,0,0}, {0.5,0,0}};

  int idx1;        			// Our desired color will be between these two indexes in "color".
  int idx2;
  float fractBetween = 0;  	// Fraction between "idx1" and "idx2" where our value is.

  if(value <= 0)	      {  idx1 = idx2 = 0;            }    // accounts for an input <=0
  else if(value >= 1)	  {  idx1 = idx2 = NUM_COLORS-1; }    // accounts for an input >=0
  else
  {
    value = value * (NUM_COLORS-1);        // Will multiply value by 3.
    idx1  = floor(value);                  // Our desired color will be after this index.
    idx2  = idx1+1;                        // ... and before this index (inclusive).
    fractBetween = value - (float)idx1;    // Distance between the two indexes (0-1).
  }

  *red   = (color[idx2][0] - color[idx1][0])*fractBetween + color[idx1][0];
  *green = (color[idx2][1] - color[idx1][1])*fractBetween + color[idx1][1];
  *blue  = (color[idx2][2] - color[idx1][2])*fractBetween + color[idx1][2];
}

static void calc_color_gradient() {
	float r, g, b;
	for (int i = 0; i <= 255; i++) {
		getHeatMapColor((float) (i) / 255, &r, &g, &b);
		colorGradient[i][0] = (int) (255 * b);
		colorGradient[i][1] = (int) (255 * g);
		colorGradient[i][2] = (int) (255 * r);
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

	fill_lut();
	double time1=0.0, tstart;
	tstart = clock();
	for (int row = 0; row < (img_height - DISP_WINDOW_HEIGTH); ++row) {
		for (int col_left = 0; col_left < (img_width - DISP_WINDOW_WIDTH);
				++col_left) {
			for (int col_right = 0; col_right < (img_width - DISP_WINDOW_WIDTH - col_left);
					++col_right) {
				guint tmp_res;
				fetch_window(DISP_WINDOW_WIDTH, DISP_WINDOW_HEIGTH,
						img_width, cmp, cmp_win);

				cmp++;
				tmp_res = get_correlation_value(ref_win, cmp_win,
				DISP_WINDOW_WIDTH * DISP_WINDOW_HEIGTH);
				if (tmp_res < min) {
					min = tmp_res;
					at_column = col_right;
//					if (min < 50000)
//						break;
				}
			}
			cmp = rrgb;

			if (at_column >= ARRAY_SIZE(lut))
				at_column = 0;

			disp_rgb->b = colorGradient[at_column * 255/25][0];
			disp_rgb->g = colorGradient[at_column * 255/25][1];
			disp_rgb->r = colorGradient[at_column * 255/25][2];
//			memcpy(disp_rgb, &lut[at_column], sizeof(*disp_rgb));
//			*disp_rgb = *lut[at_column];
//			disp_rgb->r = MIN((at_column * 10), 255);
//			disp_rgb->g = 5;
//			disp_rgb->b = 5;
			min = UINT_MAX;
			at_column = 0;
			ref++;
			cmp += (ref - lrgb);
			fetch_window(DISP_WINDOW_WIDTH, DISP_WINDOW_HEIGTH, img_width, ref, ref_win);
			disp_rgb++;
		}
		//printf("\n");
		ref += DISP_WINDOW_WIDTH;
		cmp += DISP_WINDOW_WIDTH;
		disp_rgb += DISP_WINDOW_WIDTH;
	}
	time1 += clock() - tstart;

	time1 = time1/CLOCKS_PER_SEC;

	printf("Berechnungszeit %lf sec\n", time1);

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
			"/home/sefo/devel/stereoVision/left.bmp");
	GtkWidget *rightImage = gtk_image_new_from_file(
			"/home/sefo/devel/stereoVision/right.bmp");

	GdkPixbuf *pixBufLeft = gtk_image_get_pixbuf(leftImage);
	GdkPixbuf *pixBufRight = gtk_image_get_pixbuf(rightImage);

	calculate_disparity(pixBufLeft, pixBufRight, app);

	gtk_container_set_border_width(GTK_CONTAINER(leftWindow), 25);
	gtk_container_set_border_width(GTK_CONTAINER(rightWindow), 25);
	gtk_container_add(GTK_CONTAINER(leftWindow), leftImage);
	gtk_container_add(GTK_CONTAINER(rightWindow), rightImage);
	titleLeft = g_strdup_printf("Window %d", g_slist_length(app->windows));
	titleRight = g_strdup_printf("Window %d", g_slist_length(app->windows));
	gtk_window_set_title(GTK_WINDOW(leftWindow), titleLeft);
	gtk_window_set_title(GTK_WINDOW(rightWindow), titleRight);
	g_free(titleLeft);
	g_free(titleRight);

	/* connect callbacks to signals */

	g_signal_connect(G_OBJECT (leftWindow), "destroy",
			G_CALLBACK (on_window_destroy), app);
	g_signal_connect(G_OBJECT (rightWindow), "destroy",
			G_CALLBACK (on_window_destroy), app);

	//gtk_widget_show_all(leftWindow);
	//gtk_widget_show_all(rightWindow);
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
