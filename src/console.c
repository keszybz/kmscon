/*
 * kmscon - Console Management
 * Written 2011 by David Herrmann <dh.herrmann@googlemail.com>
 */

/*
 * Console Management
 * This provides the console drawing and manipulation functions. It does not
 * provide the terminal emulation. It is just an abstraction layer to draw text
 * to a framebuffer as used by terminals and consoles.
 */

/*
 * TODO: Avoid using this hack and instead retrieve GL extension
 * pointers dynamically on initialization.
 */
#define GL_GLEXT_PROTOTYPES

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <cairo.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include "console.h"

struct kmscon_console {
	size_t ref;

	GLuint tex;
	uint32_t res_x;
	uint32_t res_y;

	cairo_t *cr;
	cairo_surface_t *surf;
	unsigned char *surf_buf;
};

int kmscon_console_new(struct kmscon_console **out)
{
	struct kmscon_console *con;

	if (!out)
		return -EINVAL;

	con = malloc(sizeof(*con));
	if (!con)
		return -ENOMEM;

	memset(con, 0, sizeof(*con));
	con->ref = 1;

	glGenTextures(1, &con->tex);

	*out = con;
	return 0;
}

void kmscon_console_ref(struct kmscon_console *con)
{
	if (!con)
		return;

	++con->ref;
}

/*
 * Drops one reference. If this is the last reference, the whole console is
 * freed and the associated render-images are destroyed.
 */
void kmscon_console_unref(struct kmscon_console *con)
{
	if (!con || !con->ref)
		return;

	if (--con->ref)
		return;

	if (con->cr) {
		cairo_destroy(con->cr);
		cairo_surface_destroy(con->surf);
		free(con->surf_buf);
	}

	glDeleteTextures(1, &con->tex);
	free(con);
}

/*
 * This resets the resolution used for drawing operations. It is recommended to
 * set this to the size of your framebuffer, howevr, you can set this to
 * anything except 0.
 * This image-resolution is used internally to render the console fonts. The
 * kmscon_console_map() function can map this image to any framebuffer size you
 * want. Therefore, this screen resolution is just a performance and quality
 * hint.
 * This function must be called before drawing the console, though. Returns 0 on
 * success, -EINVAL if con, x or y is 0/NULL and -ENOMEM on out-of-mem errors.
 */
int kmscon_console_set_res(struct kmscon_console *con, uint32_t x, uint32_t y)
{
	unsigned char *buf;
	cairo_t *cr;
	cairo_surface_t *surface;
	int stride, ret;
	cairo_format_t format = CAIRO_FORMAT_ARGB32;

	if (!con || !x || !y)
		return -EINVAL;

	stride = cairo_format_stride_for_width(format, x);

	buf = malloc(stride * y);
	if (!buf)
		return -ENOMEM;

	surface = cairo_image_surface_create_for_data(buf, format, x, y,
								stride);
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		ret = -ENOMEM;
		goto err_free;
	}

	cr = cairo_create(surface);
	if (cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
		ret = -EFAULT;
		goto err_cairo;
	}

	if (con->cr) {
		cairo_destroy(con->cr);
		cairo_surface_destroy(con->surf);
		free(con->surf_buf);
	}

	con->res_x = x;
	con->res_y = y;
	con->surf_buf = buf;
	con->surf = surface;
	con->cr = cr;

	glBindTexture(GL_TEXTURE_RECTANGLE, con->tex);
	glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA, con->res_x, con->res_y,
				0, GL_BGRA, GL_UNSIGNED_BYTE, con->surf_buf);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

	return 0;

err_cairo:
	cairo_destroy(cr);
err_free:
	cairo_surface_destroy(surface);
	free(buf);
	return ret;
}

/*
 * This redraws the console. It does not clip/copy the image onto any
 * framebuffer. You must use kmscon_console_map() to do this.
 * This allows to draw the console once and then map it onto multiple
 * framebuffers so it is displayed on multiple monitors with different screen
 * resolutions.
 * You must have called kmscon_console_set_res() before.
 */
void kmscon_console_draw(struct kmscon_console *con)
{
	if (!con || !con->cr)
		return;

	cairo_save(con->cr);

	cairo_set_operator(con->cr, CAIRO_OPERATOR_OVER);
	cairo_scale(con->cr, con->res_x, con->res_y);
	cairo_set_source_rgba(con->cr, 0.0, 0.0, 0.0, 0.0);
	cairo_paint(con->cr);

	// TODO: draw console here

	cairo_restore(con->cr);
}

/*
 * This maps the console onto the current GL framebuffer. It expects the
 * framebuffer to have 0/0 in the middle, -1/-1 in the upper left and 1/1 in the
 * lower right (default GL settings).
 * This does not clear the screen, nor does it paint the background. Instead the
 * background is transparent and blended on top of the framebuffer.
 *
 * You must have called kmscon_console_draw() before, otherwise this will map an
 * empty image onto the screen.
 */
void kmscon_console_map(struct kmscon_console *con)
{
	if (!con || !con->cr)
		return;

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_TEXTURE_RECTANGLE);
	glBindTexture(GL_TEXTURE_RECTANGLE, con->tex);

	glBegin(GL_QUADS);
		glColor4f(0.0f, 0.0f, 0.0f, 0.0f);

		glTexCoord2f(0.0f, 0.0f);
		glVertex2f(-1.0f, -1.0f);

		glTexCoord2f(con->res_x, 0.0f);
		glVertex2f(1.0f, -1.0f);

		glTexCoord2f(con->res_x, con->res_y);
		glVertex2f(1.0f, 1.0f);

		glTexCoord2f(0.0f, con->res_y);
		glVertex2f(-1.0f, 1.0f);
	glEnd();
}