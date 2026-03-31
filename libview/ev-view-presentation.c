/* ev-view-presentation.c
 *  this file is part of xreader, a mate document viewer
 *
 * Copyright (C) 2010 Carlos Garcia Campos <carlosgc@gnome.org>
 *
 * Xreader is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xreader is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include <stdlib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "ev-view-presentation.h"
#include "ev-jobs.h"
#include "ev-job-scheduler.h"
#include "ev-transition-animation.h"
#include "ev-view-cursor.h"
#include "ev-page-cache.h"

enum {
	PROP_0,
	PROP_DOCUMENT,
	PROP_CURRENT_PAGE,
	PROP_ROTATION,
	PROP_INVERTED_COLORS
};

enum {
	CHANGE_PAGE,
	FINISHED,
	SIGNAL_EXTERNAL_LINK,
	N_SIGNALS
};

typedef enum {
	EV_PRESENTATION_NORMAL,
	EV_PRESENTATION_BLACK,
	EV_PRESENTATION_WHITE,
	EV_PRESENTATION_END
} EvPresentationState;

struct _EvViewPresentation
{
	GtkWidget base;

        guint                  is_constructing : 1;

	guint                  current_page;
	cairo_surface_t       *current_surface;
	EvDocument            *document;
	guint                  rotation;
	gboolean               inverted_colors;
	EvPresentationState    state;
	gdouble                scale;
	gint                   monitor_width;
	gint                   monitor_height;

	/* Cursors */
	EvViewCursor           cursor;
	guint                  hide_cursor_timeout_id;

	/* Goto Window */
	GtkWidget             *goto_window;
	GtkWidget             *goto_entry;

	/* Page Transition */
	guint                  trans_timeout_id;

	/* Animations */
	gboolean               enable_animations;
	EvTransitionAnimation *animation;

	/* Links */
	EvPageCache           *page_cache;

	EvJob *prev_job;
	EvJob *curr_job;
	EvJob *next_job;
};

struct _EvViewPresentationClass
{
	GtkWidgetClass base_class;

	/* signals */
	void (* change_page)   (EvViewPresentation *pview,
                                GtkScrollType       scroll);
	void (* finished)      (EvViewPresentation *pview);
	void (* external_link) (EvViewPresentation *pview,
                                EvLinkAction       *action);
};

static guint signals[N_SIGNALS] = { 0 };

static void ev_view_presentation_set_cursor_for_location (EvViewPresentation *pview,
							  gdouble             x,
							  gdouble             y);
static void ev_view_presentation_snapshot (GtkWidget *widget, GtkSnapshot *snapshot);
static void ev_view_presentation_measure (GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural, int *minimum_baseline, int *natural_baseline);
static void ev_view_presentation_size_allocate (GtkWidget *widget, int width, int height, int baseline);
static gboolean ev_view_presentation_key_pressed (GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);

#define HIDE_CURSOR_TIMEOUT 5

G_DEFINE_TYPE (EvViewPresentation, ev_view_presentation, GTK_TYPE_WIDGET)

static void
ev_view_presentation_set_normal (EvViewPresentation *pview)
{
	GtkWidget *widget = GTK_WIDGET (pview);

	if (pview->state == EV_PRESENTATION_NORMAL)
		return;

	pview->state = EV_PRESENTATION_NORMAL;
	gtk_style_context_remove_class (gtk_widget_get_style_context (widget), "white-mode");
	gtk_widget_queue_draw (widget);
}



static void
ev_view_presentation_set_end (EvViewPresentation *pview)
{
	GtkWidget *widget = GTK_WIDGET (pview);

	if (pview->state == EV_PRESENTATION_END)
		return;

	pview->state = EV_PRESENTATION_END;
	gtk_widget_queue_draw (widget);
}

static gdouble
ev_view_presentation_get_scale_for_page (EvViewPresentation *pview,
					 guint               page)
{
	if (!ev_document_is_page_size_uniform (pview->document) || pview->scale == 0) {
		gdouble width, height;

		ev_document_get_page_size (pview->document, page, &width, &height);
		if (pview->rotation == 90 || pview->rotation == 270) {
			gdouble tmp;

			tmp = width;
			width = height;
			height = tmp;
		}
		pview->scale = MIN (pview->monitor_width / width, pview->monitor_height / height);
	}

	return pview->scale;
}

static void
ev_view_presentation_get_page_area (EvViewPresentation *pview,
				    GdkRectangle       *area)
{
	GtkWidget    *widget = GTK_WIDGET (pview);
	GtkAllocation allocation;
	gdouble       doc_width, doc_height;
	gint          view_width, view_height;
	gdouble       scale;

	ev_document_get_page_size (pview->document,
				   pview->current_page,
				   &doc_width, &doc_height);
	scale = ev_view_presentation_get_scale_for_page (pview, pview->current_page);

	if (pview->rotation == 90 || pview->rotation == 270) {
		view_width = (gint)((doc_height * scale) + 0.5);
		view_height = (gint)((doc_width * scale) + 0.5);
	} else {
		view_width = (gint)((doc_width * scale) + 0.5);
		view_height = (gint)((doc_height * scale) + 0.5);
	}

	gtk_widget_get_allocation (widget, &allocation);

	area->x = (MAX (0, allocation.width - view_width)) / 2;
	area->y = (MAX (0, allocation.height - view_height)) / 2;
	area->width = view_width;
	area->height = view_height;
}

/* Page Transition */
static gboolean
transition_next_page (EvViewPresentation *pview)
{
	ev_view_presentation_next_page (pview);

	return FALSE;
}

static void
ev_view_presentation_transition_stop (EvViewPresentation *pview)
{
	if (pview->trans_timeout_id > 0)
		g_source_remove (pview->trans_timeout_id);
	pview->trans_timeout_id = 0;
}

static void
ev_view_presentation_transition_start (EvViewPresentation *pview)
{
	gdouble duration;

	if (!EV_IS_DOCUMENT_TRANSITION (pview->document))
		return;

	ev_view_presentation_transition_stop (pview);

	duration = ev_document_transition_get_page_duration (EV_DOCUMENT_TRANSITION (pview->document),
							     pview->current_page);
	if (duration >= 0) {
		        pview->trans_timeout_id =
				g_timeout_add_seconds (duration,
						       (GSourceFunc) transition_next_page,
						       pview);
	}
}

/* Animations */
static void
ev_view_presentation_animation_cancel (EvViewPresentation *pview)
{
	if (pview->animation) {
		g_object_unref (pview->animation);
		pview->animation = NULL;
	}
}

static void
ev_view_presentation_transition_animation_finish (EvViewPresentation *pview)
{
	ev_view_presentation_animation_cancel (pview);
	ev_view_presentation_transition_start (pview);
	gtk_widget_queue_draw (GTK_WIDGET (pview));
}

static void
ev_view_presentation_transition_animation_frame (EvViewPresentation *pview,
						 gdouble             progress)
{
	gtk_widget_queue_draw (GTK_WIDGET (pview));
}

static void
ev_view_presentation_animation_start (EvViewPresentation *pview,
				      gint                new_page)
{
	EvTransitionEffect *effect = NULL;
	cairo_surface_t    *surface;
	gint                jump;

	if (!pview->enable_animations)
		return;

	if (pview->current_page == new_page)
		return;

	effect = ev_document_transition_get_effect (EV_DOCUMENT_TRANSITION (pview->document),
						    new_page);
	if (!effect)
		return;

	pview->animation = ev_transition_animation_new (effect);

	surface = pview->curr_job ? EV_JOB_RENDER (pview->curr_job)->surface : NULL;
	ev_transition_animation_set_origin_surface (pview->animation,
						    surface != NULL ?
						    surface : pview->current_surface);

	jump = new_page - pview->current_page;
	if (jump == -1)
		surface = pview->prev_job ? EV_JOB_RENDER (pview->prev_job)->surface : NULL;
	else if (jump == 1)
		surface = pview->next_job ? EV_JOB_RENDER (pview->next_job)->surface : NULL;
	else
		surface = NULL;
	if (surface)
		ev_transition_animation_set_dest_surface (pview->animation, surface);

	g_signal_connect_swapped (pview->animation, "frame",
				  G_CALLBACK (ev_view_presentation_transition_animation_frame),
				  pview);
	g_signal_connect_swapped (pview->animation, "finished",
				  G_CALLBACK (ev_view_presentation_transition_animation_finish),
				  pview);
}

/* Page Navigation */
static void
job_finished_cb (EvJob              *job,
		 EvViewPresentation *pview)
{
	EvJobRender *job_render = EV_JOB_RENDER (job);

	if (pview->inverted_colors)
		ev_document_misc_invert_surface (job_render->surface);

	if (job != pview->curr_job)
		return;

	if (pview->animation) {
		ev_transition_animation_set_dest_surface (pview->animation,
							  job_render->surface);
	} else {
		ev_view_presentation_transition_start (pview);
		gtk_widget_queue_draw (GTK_WIDGET (pview));
	}
}

static EvJob *
ev_view_presentation_schedule_new_job (EvViewPresentation *pview,
				       gint                page,
				       EvJobPriority       priority)
{
	EvJob  *job;
	gdouble scale;

	if (page < 0 || page >= ev_document_get_n_pages (pview->document))
		return NULL;

	scale = ev_view_presentation_get_scale_for_page (pview, page);
	job = ev_job_render_new (pview->document, page, pview->rotation, scale, 0, 0);
	g_signal_connect (job, "finished",
			  G_CALLBACK (job_finished_cb),
			  pview);
	ev_job_scheduler_push_job (job, priority);

	return job;
}

static void
ev_view_presentation_delete_job (EvViewPresentation *pview,
				 EvJob              *job)
{
	if (!job)
		return;

	g_signal_handlers_disconnect_by_func (job, job_finished_cb, pview);
	ev_job_cancel (job);
	g_object_unref (job);
}

static void
ev_view_presentation_reset_jobs (EvViewPresentation *pview)
{
        if (pview->curr_job) {
                ev_view_presentation_delete_job (pview, pview->curr_job);
                pview->curr_job = NULL;
        }

        if (pview->prev_job) {
                ev_view_presentation_delete_job (pview, pview->prev_job);
                pview->prev_job = NULL;
        }

        if (pview->next_job) {
                ev_view_presentation_delete_job (pview, pview->next_job);
                pview->next_job = NULL;
        }
}

static void
ev_view_presentation_update_current_page (EvViewPresentation *pview,
					  guint               page)
{
	gint jump;

	if (page < 0 || page >= ev_document_get_n_pages (pview->document))
		return;

	ev_view_presentation_animation_cancel (pview);
	ev_view_presentation_animation_start (pview, page);

	jump = page - pview->current_page;

	switch (jump) {
	case 0:
		if (!pview->curr_job)
			pview->curr_job = ev_view_presentation_schedule_new_job (pview, page, EV_JOB_PRIORITY_URGENT);
		if (!pview->next_job)
			pview->next_job = ev_view_presentation_schedule_new_job (pview, page + 1, EV_JOB_PRIORITY_HIGH);
		if (!pview->prev_job)
			pview->prev_job = ev_view_presentation_schedule_new_job (pview, page - 1, EV_JOB_PRIORITY_LOW);
		break;
	case -1:
		ev_view_presentation_delete_job (pview, pview->next_job);
		pview->next_job = pview->curr_job;
		pview->curr_job = pview->prev_job;

		if (!pview->curr_job)
			pview->curr_job = ev_view_presentation_schedule_new_job (pview, page, EV_JOB_PRIORITY_URGENT);
		else
			ev_job_scheduler_update_job (pview->curr_job, EV_JOB_PRIORITY_URGENT);
		pview->prev_job = ev_view_presentation_schedule_new_job (pview, page - 1, EV_JOB_PRIORITY_HIGH);
		ev_job_scheduler_update_job (pview->next_job, EV_JOB_PRIORITY_LOW);

		break;
	case 1:
		ev_view_presentation_delete_job (pview, pview->prev_job);
		pview->prev_job = pview->curr_job;
		pview->curr_job = pview->next_job;

		if (!pview->curr_job)
			pview->curr_job = ev_view_presentation_schedule_new_job (pview, page, EV_JOB_PRIORITY_URGENT);
		else
			ev_job_scheduler_update_job (pview->curr_job, EV_JOB_PRIORITY_URGENT);
		pview->next_job = ev_view_presentation_schedule_new_job (pview, page + 1, EV_JOB_PRIORITY_HIGH);
		ev_job_scheduler_update_job (pview->prev_job, EV_JOB_PRIORITY_LOW);

		break;
	case -2:
		ev_view_presentation_delete_job (pview, pview->next_job);
		ev_view_presentation_delete_job (pview, pview->curr_job);
		pview->next_job = pview->prev_job;

		pview->curr_job = ev_view_presentation_schedule_new_job (pview, page, EV_JOB_PRIORITY_URGENT);
		pview->prev_job = ev_view_presentation_schedule_new_job (pview, page - 1, EV_JOB_PRIORITY_HIGH);
		if (!pview->next_job)
			pview->next_job = ev_view_presentation_schedule_new_job (pview, page + 1, EV_JOB_PRIORITY_LOW);
		else
			ev_job_scheduler_update_job (pview->next_job, EV_JOB_PRIORITY_LOW);
		break;
	case 2:
		ev_view_presentation_delete_job (pview, pview->prev_job);
		ev_view_presentation_delete_job (pview, pview->curr_job);
		pview->prev_job = pview->next_job;

		pview->curr_job = ev_view_presentation_schedule_new_job (pview, page, EV_JOB_PRIORITY_URGENT);
		pview->next_job = ev_view_presentation_schedule_new_job (pview, page + 1, EV_JOB_PRIORITY_HIGH);
		if (!pview->prev_job)
			pview->prev_job = ev_view_presentation_schedule_new_job (pview, page - 1, EV_JOB_PRIORITY_LOW);
		else
			ev_job_scheduler_update_job (pview->prev_job, EV_JOB_PRIORITY_LOW);
		break;
	default:
		ev_view_presentation_delete_job (pview, pview->prev_job);
		ev_view_presentation_delete_job (pview, pview->curr_job);
		ev_view_presentation_delete_job (pview, pview->next_job);

		pview->curr_job = ev_view_presentation_schedule_new_job (pview, page, EV_JOB_PRIORITY_URGENT);
		if (jump > 0) {
			pview->next_job = ev_view_presentation_schedule_new_job (pview, page + 1, EV_JOB_PRIORITY_HIGH);
			pview->prev_job = ev_view_presentation_schedule_new_job (pview, page - 1, EV_JOB_PRIORITY_LOW);
		} else {
			pview->prev_job = ev_view_presentation_schedule_new_job (pview, page - 1, EV_JOB_PRIORITY_HIGH);
			pview->next_job = ev_view_presentation_schedule_new_job (pview, page + 1, EV_JOB_PRIORITY_LOW);
		}
	}

	pview->current_page = page;

	if (pview->page_cache)
		ev_page_cache_set_page_range (pview->page_cache, page, page);

	if (pview->cursor != EV_VIEW_CURSOR_HIDDEN) {
		gint x, y;

		ev_document_misc_get_pointer_position (GTK_WIDGET (pview), &x, &y);
		ev_view_presentation_set_cursor_for_location (pview, x, y);
	}

	if (EV_JOB_RENDER (pview->curr_job)->surface)
		gtk_widget_queue_draw (GTK_WIDGET (pview));
}

void
ev_view_presentation_next_page (EvViewPresentation *pview)
{
	guint n_pages;
	gint  new_page;

	switch (pview->state) {
	case EV_PRESENTATION_BLACK:
	case EV_PRESENTATION_WHITE:
		ev_view_presentation_set_normal (pview);
	case EV_PRESENTATION_END:
		return;
	case EV_PRESENTATION_NORMAL:
		break;
	}

	n_pages = ev_document_get_n_pages (pview->document);
	new_page = pview->current_page + 1;

	if (new_page == n_pages)
		ev_view_presentation_set_end (pview);
	else
		ev_view_presentation_update_current_page (pview, new_page);
}

void
ev_view_presentation_previous_page (EvViewPresentation *pview)
{
	gint new_page = 0;

	switch (pview->state) {
	case EV_PRESENTATION_BLACK:
	case EV_PRESENTATION_WHITE:
		ev_view_presentation_set_normal (pview);
		return;
	case EV_PRESENTATION_END:
		pview->state = EV_PRESENTATION_NORMAL;
		new_page = pview->current_page;
		break;
	case EV_PRESENTATION_NORMAL:
		new_page = pview->current_page - 1;
		break;
	}

	ev_view_presentation_update_current_page (pview, new_page);
}

/* Goto Window handlers removed - GTK3 events */


/* Links */
static gboolean
ev_view_presentation_link_is_supported (EvViewPresentation *pview,
					EvLink             *link)
{
	EvLinkAction *action;

	action = ev_link_get_action (link);
	if (!action)
		return FALSE;

	switch (ev_link_action_get_action_type (action)) {
	case EV_LINK_ACTION_TYPE_GOTO_DEST:
		return ev_link_action_get_dest (action) != NULL;
	case EV_LINK_ACTION_TYPE_NAMED:
	case EV_LINK_ACTION_TYPE_GOTO_REMOTE:
	case EV_LINK_ACTION_TYPE_EXTERNAL_URI:
	case EV_LINK_ACTION_TYPE_LAUNCH:
		return TRUE;
	default:
		return FALSE;
	}

	return FALSE;
}

static EvLink *
ev_view_presentation_get_link_at_location (EvViewPresentation *pview,
					   gdouble             x,
					   gdouble             y)
{
	GdkRectangle   page_area;
	EvMappingList *link_mapping;
	EvLink        *link;
	gdouble        width, height;
	gdouble        new_x, new_y;
	gdouble        scale;

	if (!pview->page_cache)
		return NULL;

	ev_document_get_page_size (pview->document, pview->current_page, &width, &height);
	ev_view_presentation_get_page_area (pview, &page_area);
	scale = ev_view_presentation_get_scale_for_page (pview, pview->current_page);
	x = (x - page_area.x) / scale;
	y = (y - page_area.y) / scale;
	switch (pview->rotation) {
	case 0:
	case 360:
		new_x = x;
		new_y = y;
		break;
	case 90:
		new_x = y;
		new_y = height - x;
		break;
	case 180:
		new_x = width - x;
		new_y = height - y;
		break;
	case 270:
		new_x = width - y;
		new_y = x;
		break;
	default:
		g_assert_not_reached ();
	}

	link_mapping = ev_page_cache_get_link_mapping (pview->page_cache, pview->current_page);

	link = link_mapping ? ev_mapping_list_get_data (link_mapping, new_x, new_y) : NULL;

	return link && ev_view_presentation_link_is_supported (pview, link) ? link : NULL;
}


static void
ev_view_presentation_snapshot (GtkWidget   *widget,
			       GtkSnapshot *snapshot)
{
	EvViewPresentation *pview = EV_VIEW_PRESENTATION (widget);
	cairo_t            *cr;
	GdkRectangle        page_area;
	const GdkRGBA       black = { 0, 0, 0, 1 };
	const GdkRGBA       white = { 1, 1, 1, 1 };
	graphene_rect_t     bounds;

    bounds.origin.x = 0; bounds.origin.y = 0;
    bounds.size.width = gtk_widget_get_width (widget);
    bounds.size.height = gtk_widget_get_height (widget);

	switch (pview->state) {
	case EV_PRESENTATION_BLACK:
		gtk_snapshot_append_color (snapshot, &black, &bounds);
		return;
	case EV_PRESENTATION_WHITE:
		gtk_snapshot_append_color (snapshot, &white, &bounds);
		return;
	case EV_PRESENTATION_NORMAL:
	case EV_PRESENTATION_END:
		gtk_snapshot_append_color (snapshot, &black, &bounds);
		break;
	}

	if (pview->state == EV_PRESENTATION_END) {
		return;
    }

	if (!pview->current_surface && pview->curr_job && EV_JOB_RENDER (pview->curr_job)->surface) {
		pview->current_surface = cairo_surface_reference (EV_JOB_RENDER (pview->curr_job)->surface);
	}

	if (!pview->current_surface)
		return;

	ev_view_presentation_get_page_area (pview, &page_area);

    cr = gtk_snapshot_append_cairo (snapshot, &bounds);

	if (pview->animation) {
		ev_transition_animation_paint (pview->animation, cr, page_area);
	} else {
		cairo_set_source_surface (cr, pview->current_surface, page_area.x, page_area.y);
		cairo_paint (cr);
	}

	cairo_destroy (cr);
}

static void
ev_view_presentation_measure (GtkWidget      *widget,
			      GtkOrientation  orientation,
			      int             for_size,
			      int            *minimum,
			      int            *natural,
			      int            *minimum_baseline,
			      int            *natural_baseline)
{
	*minimum = *natural = 0;
}

static void
ev_view_presentation_size_allocate (GtkWidget *widget,
				    int        width,
				    int        height,
				    int        baseline)
{
	EvViewPresentation *pview = EV_VIEW_PRESENTATION (widget);

	pview->monitor_width = width;
	pview->monitor_height = height;
    pview->scale = 0;

	if (pview->document) {
		ev_view_presentation_reset_jobs (pview);
		ev_view_presentation_update_current_page (pview, pview->current_page);
	}
}

static gboolean
ev_view_presentation_key_pressed (GtkEventControllerKey *controller,
				  guint                  keyval,
				  guint                  keycode,
				  GdkModifierType        state,
				  gpointer               user_data)
{
	EvViewPresentation *pview = EV_VIEW_PRESENTATION (user_data);

	switch (keyval) {
	case GDK_KEY_Escape:
		g_signal_emit (pview, signals[FINISHED], 0);
		return TRUE;
	case GDK_KEY_space:
	case GDK_KEY_Page_Down:
	case GDK_KEY_Down:
	case GDK_KEY_Right:
		ev_view_presentation_next_page (pview);
		return TRUE;
	case GDK_KEY_BackSpace:
	case GDK_KEY_Page_Up:
	case GDK_KEY_Up:
	case GDK_KEY_Left:
		ev_view_presentation_previous_page (pview);
		return TRUE;
	}

	return FALSE;
}

/* Cursors */
static void
ev_view_presentation_set_cursor (EvViewPresentation *pview,
				 EvViewCursor        view_cursor)
{
	GtkWidget  *widget;
	GdkCursor  *cursor;

	if (pview->cursor == view_cursor)
		return;

	widget = GTK_WIDGET (pview);
	if (!gtk_widget_get_realized (widget))
		gtk_widget_realize (widget);

	pview->cursor = view_cursor;

	cursor = ev_view_cursor_new (gtk_widget_get_display (widget), view_cursor);
	gtk_widget_set_cursor (widget, cursor);
	// gdk_flush ();
	if (cursor)
		g_object_unref (cursor);
}

static void
ev_view_presentation_set_cursor_for_location (EvViewPresentation *pview,
					      gdouble             x,
					      gdouble             y)
{
	if (ev_view_presentation_get_link_at_location (pview, x, y))
		ev_view_presentation_set_cursor (pview, EV_VIEW_CURSOR_LINK);
	else
		ev_view_presentation_set_cursor (pview, EV_VIEW_CURSOR_NORMAL);
}





static void
ev_view_presentation_dispose (GObject *object)
{
	EvViewPresentation *pview = EV_VIEW_PRESENTATION (object);

	if (pview->document) {
		g_object_unref (pview->document);
		pview->document = NULL;
	}

	ev_view_presentation_animation_cancel (pview);
	ev_view_presentation_transition_stop (pview);
	// ev_view_presentation_hide_cursor_timeout_stop (pview);
        ev_view_presentation_reset_jobs (pview);

	if (pview->current_surface) {
		cairo_surface_destroy (pview->current_surface);
		pview->current_surface = NULL;
	}

	if (pview->page_cache) {
		g_object_unref (pview->page_cache);
		pview->page_cache = NULL;
	}

	if (pview->goto_window) {
		pview->goto_window = NULL;
		pview->goto_entry = NULL;
	}

	G_OBJECT_CLASS (ev_view_presentation_parent_class)->dispose (object);
}



static void
ev_view_presentation_set_property (GObject      *object,
				   guint         prop_id,
				   const GValue *value,
				   GParamSpec   *pspec)
{
	EvViewPresentation *pview = EV_VIEW_PRESENTATION (object);

	switch (prop_id) {
	case PROP_DOCUMENT:
		pview->document = g_value_dup_object (value);
		pview->enable_animations = EV_IS_DOCUMENT_TRANSITION (pview->document);
		break;
	case PROP_CURRENT_PAGE:
		pview->current_page = g_value_get_uint (value);
		break;
	case PROP_ROTATION:
                ev_view_presentation_set_rotation (pview, g_value_get_uint (value));
		break;
	case PROP_INVERTED_COLORS:
		pview->inverted_colors = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
ev_view_presentation_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
        EvViewPresentation *pview = EV_VIEW_PRESENTATION (object);

        switch (prop_id) {
        case PROP_ROTATION:
                g_value_set_uint (value, ev_view_presentation_get_rotation (pview));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        }
}

static GObject *
ev_view_presentation_constructor (GType                  type,
				  guint                  n_construct_properties,
				  GObjectConstructParam *construct_params)
{
	GObject            *object;
	EvViewPresentation *pview;

	object = G_OBJECT_CLASS (ev_view_presentation_parent_class)->constructor (type,
										  n_construct_properties,
										  construct_params);
	pview = EV_VIEW_PRESENTATION (object);
        pview->is_constructing = FALSE;

	if (EV_IS_DOCUMENT_LINKS (pview->document)) {
		pview->page_cache = ev_page_cache_new (pview->document);
		ev_page_cache_set_flags (pview->page_cache, EV_PAGE_DATA_INCLUDE_LINKS);
	}

	return object;
}

static void
ev_view_presentation_class_init (EvViewPresentationClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GObjectClass   *gobject_class = G_OBJECT_CLASS (klass);


	

	widget_class->snapshot = ev_view_presentation_snapshot;
	widget_class->measure = ev_view_presentation_measure;
	widget_class->size_allocate = ev_view_presentation_size_allocate;

	gtk_widget_class_set_css_name (widget_class, "evpresentationview");

	gobject_class->dispose = ev_view_presentation_dispose;

	gobject_class->constructor = ev_view_presentation_constructor;
	gobject_class->set_property = ev_view_presentation_set_property;
	gobject_class->get_property = ev_view_presentation_get_property;

	g_object_class_install_property (gobject_class,
					 PROP_DOCUMENT,
					 g_param_spec_object ("document",
							      "Document",
							      "Document",
							      EV_TYPE_DOCUMENT,
							      G_PARAM_WRITABLE |
							      G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (gobject_class,
					 PROP_CURRENT_PAGE,
					 g_param_spec_uint ("current_page",
							    "Current Page",
							    "The current page",
							    0, G_MAXUINT, 0,
							    G_PARAM_WRITABLE |
							    G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (gobject_class,
					 PROP_ROTATION,
					 g_param_spec_uint ("rotation",
							    "Rotation",
							    "Current rotation angle",
							    0, 360, 0,
							    G_PARAM_READWRITE |
							    G_PARAM_CONSTRUCT));
	g_object_class_install_property (gobject_class,
					 PROP_INVERTED_COLORS,
					 g_param_spec_boolean ("inverted_colors",
							       "Inverted Colors",
							       "Whether presentation is displayed with inverted colors",
							       FALSE,
							       G_PARAM_WRITABLE |
							       G_PARAM_CONSTRUCT_ONLY));

	signals[CHANGE_PAGE] =
		g_signal_new ("change_page",
			      G_OBJECT_CLASS_TYPE (gobject_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (EvViewPresentationClass, change_page),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__ENUM,
			      G_TYPE_NONE, 1,
			      GTK_TYPE_SCROLL_TYPE);
	signals[FINISHED] =
		g_signal_new ("finished",
			      G_OBJECT_CLASS_TYPE (gobject_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (EvViewPresentationClass, finished),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0,
			      G_TYPE_NONE);
	signals[SIGNAL_EXTERNAL_LINK] =
		g_signal_new ("external-link",
			      G_TYPE_FROM_CLASS (gobject_class),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (EvViewPresentationClass, external_link),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      G_TYPE_OBJECT);


}

static void
ev_view_presentation_init (EvViewPresentation *pview)
{
	GtkEventController *controller;

	gtk_widget_set_can_focus (GTK_WIDGET (pview), TRUE);
	pview->is_constructing = TRUE;

	controller = gtk_event_controller_key_new ();
	g_signal_connect (controller, "key-pressed",
			  G_CALLBACK (ev_view_presentation_key_pressed), pview);
	gtk_widget_add_controller (GTK_WIDGET (pview), controller);
}

GtkWidget *
ev_view_presentation_new (EvDocument *document,
			  guint       current_page,
			  guint       rotation,
			  gboolean    inverted_colors)
{
	g_return_val_if_fail (EV_IS_DOCUMENT (document), NULL);
	g_return_val_if_fail (current_page < ev_document_get_n_pages (document), NULL);

	return GTK_WIDGET (g_object_new (EV_TYPE_VIEW_PRESENTATION,
					 "document", document,
					 "current_page", current_page,
					 "rotation", rotation,
					 "inverted_colors", inverted_colors,
					 NULL));
}

void
ev_view_presentation_set_rtl (EvViewPresentation *pview, gboolean rtl)
{
        // Directionality is handled via gtk_widget_set_direction or automatically in GTK4.
        // For Key binding changes we would intercept in the key controller directly instead of modifying class bindings dynamically.
}

guint
ev_view_presentation_get_current_page (EvViewPresentation *pview)
{
	return pview->current_page;
}

void
ev_view_presentation_set_rotation (EvViewPresentation *pview,
                                   gint                rotation)
{
        if (rotation >= 360)
                rotation -= 360;
        else if (rotation < 0)
                rotation += 360;

        if (pview->rotation == rotation)
                return;

        pview->rotation = rotation;
        g_object_notify (G_OBJECT (pview), "rotation");
        if (pview->is_constructing)
                return;

        pview->scale = 0;
        ev_view_presentation_reset_jobs (pview);
        ev_view_presentation_update_current_page (pview, pview->current_page);
}

guint
ev_view_presentation_get_rotation (EvViewPresentation *pview)
{
        return pview->rotation;
}
