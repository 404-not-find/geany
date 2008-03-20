/*
 *      utils.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2005-2008 Enrico Tröger <enrico(dot)troeger(at)uvena(dot)de>
 *      Copyright 2006-2008 Nick Treleaven <nick(dot)treleaven(at)btinternet(dot)com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * $Id$
 */

/*
 * General utility functions, non-GTK related.
 */

#include "SciLexer.h"
#include "geany.h"

#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#include <glib/gstdio.h>

#include "prefs.h"
#include "support.h"
#include "document.h"
#include "filetypes.h"
#include "sciwrappers.h"
#include "dialogs.h"
#include "win32.h"
#include "project.h"

#include "utils.h"


void utils_start_browser(const gchar *uri)
{
#ifdef G_OS_WIN32
	win32_open_browser(uri);
#else
	const gchar *argv[3];

	argv[0] = prefs.tools_browser_cmd;
	argv[1] = uri;
	argv[2] = NULL;

	if (! g_spawn_async(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL))
	{
		argv[0] = "firefox";
		if (! g_spawn_async(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL))
		{
			argv[0] = "mozilla";
			if (! g_spawn_async(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL))
			{
				argv[0] = "opera";
				if (! g_spawn_async(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL))
				{
					argv[0] = "konqueror";
					if (! g_spawn_async(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL))
					{
						argv[0] = "netscape";
						g_spawn_async(NULL, (gchar**)argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
					}
				}
			}
		}
	}
#endif
}


/* taken from anjuta, to determine the EOL mode of the file */
gint utils_get_line_endings(const gchar* buffer, glong size)
{
	gint i;
	guint cr, lf, crlf, max_mode;
	gint mode;

	cr = lf = crlf = 0;

	for ( i = 0; i < size ; i++ )
	{
		if ( buffer[i] == 0x0a )
		{
			/* LF */
			lf++;
		}
		else if ( buffer[i] == 0x0d )
		{
			if (i >= (size-1))
			{
				/* Last char, CR */
				cr++;
			}
			else
			{
				if (buffer[i+1] != 0x0a)
				{
					/* CR */
					cr++;
				}
				else
				{
					/* CRLF */
					crlf++;
				}
				i++;
			}
		}
	}

	/* Vote for the maximum */
	mode = SC_EOL_LF;
	max_mode = lf;
	if (crlf > max_mode)
	{
		mode = SC_EOL_CRLF;
		max_mode = crlf;
	}
	if (cr > max_mode)
	{
		mode = SC_EOL_CR;
		max_mode = cr;
	}

	return mode;
}


gboolean utils_isbrace(gchar c, gboolean include_angles)
{
	switch (c)
	{
		case '<':
		case '>':
		return include_angles;

		case '(':
		case ')':
		case '{':
		case '}':
		case '[':
		case ']': return TRUE;
		default:  return FALSE;
	}
}


gboolean utils_is_opening_brace(gchar c, gboolean include_angles)
{
	switch (c)
	{
		case '<':
		return include_angles;

		case '(':
		case '{':
		case '[':  return TRUE;
		default:  return FALSE;
	}
}


/* line is counted with 1 as the first line, not 0 */
gboolean utils_goto_file_line(const gchar *file, gboolean is_tm_filename, gint line)
{
	gint file_idx = document_find_by_filename(file, is_tm_filename);

	if (file_idx < 0) return FALSE;

	return utils_goto_line(file_idx, line);
}


/* line is counted with 1 as the first line, not 0 */
gboolean utils_goto_line(gint idx, gint line)
{
	gint page_num;

	line--;	/* the user counts lines from 1, we begin at 0 so bring the user line to our one */

	if (idx == -1 || ! doc_list[idx].is_valid || line < 0)
		return FALSE;

	/* mark the tag */
	sci_marker_delete_all(doc_list[idx].sci, 0);
	sci_set_marker_at_line(doc_list[idx].sci, line, TRUE, 0);

	sci_goto_line(doc_list[idx].sci, line, TRUE);
	doc_list[idx].scroll_percent = 0.25F;

	/* finally switch to the page */
	page_num = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), GTK_WIDGET(doc_list[idx].sci));
	gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page_num);

	return TRUE;
}


/**
 *  Write the given @c text into a file with @c filename.
 *  If the file doesn't exist, it will be created.
 *  If it already exists, it will be overwritten.
 *
 *  @param filename The filename of the file to write, in locale encoding.
 *  @param text The text to write into the file.
 *
 *  @return 0 if the directory was successfully created, otherwise the @c errno of the
 *          failed operation is returned.
 **/
gint utils_write_file(const gchar *filename, const gchar *text)
{
	FILE *fp;
	gint bytes_written, len;

	if (filename == NULL)
	{
		return ENOENT;
	}

	len = strlen(text);

	fp = g_fopen(filename, "w");
	if (fp != NULL)
	{
		bytes_written = fwrite(text, sizeof (gchar), len, fp);
		fclose(fp);

		if (len != bytes_written)
		{
			geany_debug("utils_write_file(): written only %d bytes, had to write %d bytes to %s",
								bytes_written, len, filename);
			return EIO;
		}
	}
	else
	{
		geany_debug("utils_write_file(): could not write to file %s (%s)",
			filename, g_strerror(errno));
		return errno;
	}
	return 0;
}


/*
 * (stolen from anjuta and modified)
 * Search backward through size bytes looking for a '<', then return the tag if any
 * @return The tag name
 */
gchar *utils_find_open_xml_tag(const gchar sel[], gint size, gboolean check_tag)
{
	/* 40 chars per tag should be enough, or not? */
	gint i = 0, max_tag_size = 40;
	gchar *result = g_malloc(max_tag_size);
	const gchar *begin, *cur;

	if (size < 3)
	{
		/* Smallest tag is "<p>" which is 3 characters */
		return result;
	}
	begin = &sel[0];
	if (check_tag)
		cur = &sel[size - 3];
	else
		cur = &sel[size - 1];

	cur--; /* Skip past the > */
	while (cur > begin)
	{
		if (*cur == '<') break;
		else if (! check_tag && *cur == '>') break;
		--cur;
	}

	if (*cur == '<')
	{
		cur++;
		while((strchr(":_-.", *cur) || isalnum(*cur)) && i < (max_tag_size - 1))
		{
			result[i++] = *cur;
			cur++;
		}
	}

	result[i] = '\0';
	/* Return the tag name or "" */
	return result;
}


static gboolean reload_idx(gpointer data)
{
	gint idx = GPOINTER_TO_INT(data);

	/* check idx is still valid now we're idle, in case it was closed */
	if (DOC_IDX_VALID(idx))
	{
		document_reload_file(idx, NULL);
	}
	return FALSE;
}


static gboolean check_reload(gint idx)
{
	gchar *base_name = g_path_get_basename(doc_list[idx].file_name);
	gboolean want_reload;

	want_reload = dialogs_show_question_full(NULL, _("_Reload"), GTK_STOCK_CANCEL,
		_("Do you want to reload it?"),
		_("The file '%s' on the disk is more recent than\n"
			"the current buffer."), base_name);
	if (want_reload)
	{
		/* delay reloading because we need to wait for any pending scintilla messages
		 * to be processed, otherwise the reloaded document might not be colourised
		 * properly */
		g_idle_add(reload_idx, GINT_TO_POINTER(idx));
	}
	g_free(base_name);
	return want_reload;
}


/* Set force to force a disk check, otherwise it is ignored if there was a check
 * in the last GEANY_CHECK_FILE_DELAY seconds.
 * @return @c TRUE if the file has changed. */
gboolean utils_check_disk_status(gint idx, gboolean force)
{
	struct stat st;
	time_t t;
	gchar *locale_filename;
	gboolean ret = FALSE;
	const time_t delay_time = 10;	/* seconds to delay disk checks for */

	if (idx == -1 || doc_list[idx].file_name == NULL) return FALSE;

	t = time(NULL);

	if (! force && doc_list[idx].last_check > (t - GEANY_CHECK_FILE_DELAY)) return FALSE;

	locale_filename = utils_get_locale_from_utf8(doc_list[idx].file_name);
	if (g_stat(locale_filename, &st) != 0)
	{
		/* TODO: warn user file on disk is missing */
	}
	else if (doc_list[idx].mtime - delay_time > t || st.st_mtime > t)
	{
		geany_debug("Strange: Something is wrong with the time stamps.");
	}
	else if (doc_list[idx].mtime < st.st_mtime)
	{
		if (check_reload(idx))
			doc_list[idx].last_check = t + delay_time;	/* Disable checking until after reload */
		else
			doc_list[idx].mtime = st.st_mtime;	/* Ignore this change on disk completely */

		ret = TRUE; /* file has changed */
	}
	g_free(locale_filename);
	return ret;
}


/* This could perhaps be improved to check for #if, class etc. */
static gint get_function_fold_number(gint idx)
{
	/* for Java the functions are always one fold level above the class scope */
	if (FILETYPE_ID(doc_list[idx].file_type) == GEANY_FILETYPES_JAVA)
		return SC_FOLDLEVELBASE + 1;
	else
		return SC_FOLDLEVELBASE;
}


/* Should be used only with utils_get_current_function. */
static gboolean current_function_changed(gint cur_idx, gint cur_line, gint fold_level)
{
	static gint old_line = -2;
	static gint old_idx = -1;
	static gint old_fold_num = -1;
	const gint fold_num = fold_level & SC_FOLDLEVELNUMBERMASK;
	gboolean ret;

	/* check if the cached line and file index have changed since last time: */
	if (cur_idx < 0 || cur_idx != old_idx)
		ret = TRUE;
	else
	if (cur_line == old_line)
		ret = FALSE;
	else
	{
		/* if the line has only changed by 1 */
		if (abs(cur_line - old_line) == 1)
		{
			const gint fn_fold =
				get_function_fold_number(cur_idx);
			/* It's the same function if the fold number hasn't changed, or both the new
			 * and old fold numbers are above the function fold number. */
			gboolean same =
				fold_num == old_fold_num ||
				(old_fold_num > fn_fold && fold_num > fn_fold);

			ret = ! same;
		}
		else ret = TRUE;
	}

	/* record current line and file index for next time */
	old_line = cur_line;
	old_idx = cur_idx;
	old_fold_num = fold_num;
	return ret;
}


/* Parse the function name up to 2 lines before tag_line.
 * C++ like syntax should be parsed by parse_cpp_function_at_line, otherwise the return
 * type or argument names can be confused with the function name. */
static gchar *parse_function_at_line(ScintillaObject *sci, gint tag_line)
{
	gint start, end, max_pos;
	gchar *cur_tag;
	gint fn_style;

	switch (sci_get_lexer(sci))
	{
		case SCLEX_RUBY:	fn_style = SCE_RB_DEFNAME; break;
		case SCLEX_PYTHON:	fn_style = SCE_P_DEFNAME; break;
		default: fn_style = SCE_C_IDENTIFIER;	/* several lexers use SCE_C_IDENTIFIER */
	}
	start = sci_get_position_from_line(sci, tag_line - 2);
	max_pos = sci_get_position_from_line(sci, tag_line + 1);
	while (sci_get_style_at(sci, start) != fn_style
		&& start < max_pos) start++;

	end = start;
	while (sci_get_style_at(sci, end) == fn_style
		&& end < max_pos) end++;

	if (start == end) return NULL;
	cur_tag = g_malloc(end - start + 1);
	sci_get_text_range(sci, start, end, cur_tag);
	return cur_tag;
}


/* Parse the function name */
static gchar *parse_cpp_function_at_line(ScintillaObject *sci, gint tag_line)
{
	gint start, end, first_pos, max_pos;
	gint tmp;
	gchar c;
	gchar *cur_tag;

	first_pos = end = sci_get_position_from_line(sci, tag_line);
	max_pos = sci_get_position_from_line(sci, tag_line + 1);
	tmp = 0;
	/* goto the begin of function body */
	while (end < max_pos &&
		(tmp = sci_get_char_at(sci, end)) != '{' &&
		tmp != 0) end++;
	if (tmp == 0) end --;

	/* go back to the end of function identifier */
	while (end > 0 && end > first_pos - 500 &&
		(tmp = sci_get_char_at(sci, end)) != '(' &&
		tmp != 0) end--;
	end--;
	if (end < 0) end = 0;

	/* skip whitespaces between identifier and ( */
	while (end > 0 && isspace(sci_get_char_at(sci, end))) end--;

	start = end;
	c = 0;
	/* Use tmp to find SCE_C_IDENTIFIER or SCE_C_GLOBALCLASS chars */
	while (start >= 0 && ((tmp = sci_get_style_at(sci, start)) == SCE_C_IDENTIFIER
		 ||  tmp == SCE_C_GLOBALCLASS
		 || (c = sci_get_char_at(sci, start)) == '~'
		 ||  c == ':'))
		start--;
	if (start != 0 && start < end) start++;	/* correct for last non-matching char */

	if (start == end) return NULL;
	cur_tag = g_malloc(end - start + 2);
	sci_get_text_range(sci, start, end + 1, cur_tag);
	return cur_tag;
}


/* Sets *tagname to point at the current function or tag name.
 * If idx is -1, reset the cached current tag data to ensure it will be reparsed on the next
 * call to this function.
 * Returns: line number of the current tag, or -1 if unknown. */
gint utils_get_current_function(gint idx, const gchar **tagname)
{
	static gint tag_line = -1;
	static gchar *cur_tag = NULL;
	gint line;
	gint fold_level;
	TMWorkObject *tm_file;

	if (! DOC_IDX_VALID(idx))	/* reset current function */
	{
		current_function_changed(-1, -1, -1);
		g_free(cur_tag);
		cur_tag = g_strdup(_("unknown"));
		if (tagname != NULL)
			*tagname = cur_tag;
		tag_line = -1;
		return tag_line;
	}

	line = sci_get_current_line(doc_list[idx].sci);
	fold_level = sci_get_fold_level(doc_list[idx].sci, line);
	/* check if the cached line and file index have changed since last time: */
	if (! current_function_changed(idx, line, fold_level))
	{
		/* we can assume same current function as before */
		*tagname = cur_tag;
		return tag_line;
	}
	g_free(cur_tag); /* free the old tag, it will be replaced. */

	/* if line is at base fold level, we're not in a function */
	if ((fold_level & SC_FOLDLEVELNUMBERMASK) == SC_FOLDLEVELBASE)
	{
		cur_tag = g_strdup(_("unknown"));
		*tagname = cur_tag;
		tag_line = -1;
		return tag_line;
	}
	tm_file = doc_list[idx].tm_file;

	/* if the document has no changes, get the previous function name from TM */
	if(! doc_list[idx].changed && tm_file != NULL && tm_file->tags_array != NULL)
	{
		const TMTag *tag = (const TMTag*) tm_get_current_function(tm_file->tags_array, line);

		if (tag != NULL)
		{
			gchar *tmp;
			tmp = tag->atts.entry.scope;
			cur_tag = tmp ? g_strconcat(tmp, "::", tag->name, NULL) : g_strdup(tag->name);
			*tagname = cur_tag;
			tag_line = tag->atts.entry.line;
			return tag_line;
		}
	}

	/* parse the current function name here because TM line numbers may have changed,
	 * and it would take too long to reparse the whole file. */
	if (doc_list[idx].file_type != NULL &&
		doc_list[idx].file_type->id != GEANY_FILETYPES_ALL)
	{
		const gint fn_fold = get_function_fold_number(idx);

		tag_line = line;
		do	/* find the top level fold point */
		{
			tag_line = sci_get_fold_parent(doc_list[idx].sci, tag_line);
			fold_level = sci_get_fold_level(doc_list[idx].sci, tag_line);
		} while (tag_line >= 0 &&
			(fold_level & SC_FOLDLEVELNUMBERMASK) != fn_fold);

		if (tag_line >= 0)
		{
			if (sci_get_lexer(doc_list[idx].sci) == SCLEX_CPP)
				cur_tag = parse_cpp_function_at_line(doc_list[idx].sci, tag_line);
			else
				cur_tag = parse_function_at_line(doc_list[idx].sci, tag_line);

			if (cur_tag != NULL)
			{
				*tagname = cur_tag;
				return tag_line;
			}
		}
	}

	cur_tag = g_strdup(_("unknown"));
	*tagname = cur_tag;
	tag_line = -1;
	return tag_line;
}


/* returns the end-of-line character(s) length of the specified editor */
gint utils_get_eol_char_len(gint idx)
{
	if (idx == -1) return 0;

	switch (sci_get_eol_mode(doc_list[idx].sci))
	{
		case SC_EOL_CRLF: return 2; break;
		default: return 1; break;
	}
}


/* returns the end-of-line character(s) of the specified editor */
const gchar *utils_get_eol_char(gint idx)
{
	if (idx == -1) return '\0';

	switch (sci_get_eol_mode(doc_list[idx].sci))
	{
		case SC_EOL_CRLF: return "\r\n"; break;
		case SC_EOL_CR: return "\r"; break;
		case SC_EOL_LF:
		default: return "\n"; break;
	}
}


gboolean utils_atob(const gchar *str)
{
	if (str == NULL) return FALSE;
	else if (strcasecmp(str, "TRUE")) return FALSE;
	else return TRUE;
}


gboolean utils_is_absolute_path(const gchar *path)
{
	if (! path || *path == '\0')
		return FALSE;
#ifdef G_OS_WIN32
	if (path[0] == '\\' || path[1] == ':')
		return TRUE;
#else
	if (path[0] == '/')
		return TRUE;
#endif

	return FALSE;
}


gdouble utils_scale_round (gdouble val, gdouble factor)
{
	/*val = floor(val * factor + 0.5);*/
	val = floor(val);
	val = MAX(val, 0);
	val = MIN(val, factor);

	return val;
}


/**
 *  @a NULL-safe string comparison. Returns @a TRUE if both @c a and @c b are @a NULL
 *  or if @c a and @c b refer to valid strings which are equal.
 *
 *  @param a Pointer to first string or @a NULL.
 *  @param b Pointer to first string or @a NULL.
 *
 *  @return @a TRUE if @c a equals @c b, else @a FALSE.
 **/
gboolean utils_str_equal(const gchar *a, const gchar *b)
{
	/* (taken from libexo from os-cillation) */
	if (a == NULL && b == NULL) return TRUE;
	else if (a == NULL || b == NULL) return FALSE;

	while (*a == *b++)
		if (*a++ == '\0')
			return TRUE;

	return FALSE;
}


/**
 *  Remove the extension from @c filename and return the result in a newly allocated string.
 *
 *  @param filename The filename to operate on.
 *
 *  @return A newly-allocated string, should be freed when no longer needed.
 **/
gchar *utils_remove_ext_from_filename(const gchar *filename)
{
	gchar *last_dot = strrchr(filename, '.');
	gchar *result;
	gint i;

	if (filename == NULL) return NULL;

	if (! last_dot) return g_strdup(filename);

	/* assumes extension is small, so extra bytes don't matter */
	result = g_malloc(strlen(filename));
	i = 0;
	while ((filename + i) != last_dot)
	{
		result[i] = filename[i];
		i++;
	}
	result[i] = 0;
	return result;
}


gchar utils_brace_opposite(gchar ch)
{
	switch (ch)
	{
		case '(': return ')';
		case ')': return '(';
		case '[': return ']';
		case ']': return '[';
		case '{': return '}';
		case '}': return '{';
		case '<': return '>';
		case '>': return '<';
		default: return '\0';
	}
}


gchar *utils_get_hostname(void)
{
#ifdef G_OS_WIN32
	return win32_get_hostname();
#elif defined(HAVE_GETHOSTNAME)
	gchar hostname[100];
	if (gethostname(hostname, sizeof(hostname)) == 0)
		return g_strdup(hostname);
#endif
	return g_strdup("localhost");
}


/* Checks whether the given file can be written. locale_filename is expected in locale encoding.
 * Returns 0 if it can be written, otherwise it returns errno */
gint utils_is_file_writeable(const gchar *locale_filename)
{
	gchar *file;
	gint ret;

	if (! g_file_test(locale_filename, G_FILE_TEST_EXISTS) &&
		! g_file_test(locale_filename, G_FILE_TEST_IS_DIR))
		/* get the file's directory to check for write permission if it doesn't yet exist */
		file = g_path_get_dirname(locale_filename);
	else
		file = g_strdup(locale_filename);

#ifdef G_OS_WIN32
	/* use _waccess on Windows, access() doesn't accept special characters */
	ret = win32_check_write_permission(file);
#else

	/* access set also errno to "FILE NOT FOUND" even if locale_filename is writeable, so use
	 * errno only when access() explicitly returns an error */
	if (access(file, R_OK | W_OK) != 0)
		ret = errno;
	else
		ret = 0;
#endif
	g_free(file);
	return ret;
}


#ifdef G_OS_WIN32
# define DIR_SEP "\\" /* on Windows we need an additional dir separator */
#else
# define DIR_SEP ""
#endif

gint utils_make_settings_dir(void)
{
	gint saved_errno = 0;
	gchar *conf_file = g_strconcat(app->configdir, G_DIR_SEPARATOR_S, "geany.conf", NULL);
	gchar *filedefs_dir = g_strconcat(app->configdir, G_DIR_SEPARATOR_S,
					GEANY_FILEDEFS_SUBDIR, G_DIR_SEPARATOR_S, NULL);

	gchar *templates_dir = g_strconcat(app->configdir, G_DIR_SEPARATOR_S,
					GEANY_TEMPLATES_SUBDIR, G_DIR_SEPARATOR_S, NULL);

	if (! g_file_test(app->configdir, G_FILE_TEST_EXISTS))
	{
		geany_debug("creating config directory %s", app->configdir);
		saved_errno = utils_mkdir(app->configdir, FALSE);
	}

	if (saved_errno == 0 && ! g_file_test(conf_file, G_FILE_TEST_EXISTS))
	{	/* check whether geany.conf can be written */
		saved_errno = utils_is_file_writeable(app->configdir);
	}

	/* make subdir for filetype definitions */
	if (saved_errno == 0)
	{
		gchar *filedefs_readme = g_strconcat(app->configdir, G_DIR_SEPARATOR_S,
			GEANY_FILEDEFS_SUBDIR, G_DIR_SEPARATOR_S, "filetypes.README", NULL);

		if (! g_file_test(filedefs_dir, G_FILE_TEST_EXISTS))
		{
			saved_errno = utils_mkdir(filedefs_dir, FALSE);
		}
		if (saved_errno == 0 && ! g_file_test(filedefs_readme, G_FILE_TEST_EXISTS))
		{
			gchar *text = g_strconcat(
"Copy files from ", app->datadir, " to this directory to overwrite "
"them. To use the defaults, just delete the file in this directory.\nFor more information read "
"the documentation (in ", app->docdir, DIR_SEP "index.html or visit " GEANY_HOMEPAGE ").", NULL);
			utils_write_file(filedefs_readme, text);
			g_free(text);
		}
		g_free(filedefs_readme);
	}

	/* make subdir for template files */
	if (saved_errno == 0)
	{
		gchar *templates_readme = g_strconcat(app->configdir, G_DIR_SEPARATOR_S,
			GEANY_TEMPLATES_SUBDIR, G_DIR_SEPARATOR_S, "templates.README", NULL);

		if (! g_file_test(templates_dir, G_FILE_TEST_EXISTS))
		{
			saved_errno = utils_mkdir(templates_dir, FALSE);
		}
		if (saved_errno == 0 && ! g_file_test(templates_readme, G_FILE_TEST_EXISTS))
		{
			gchar *text = g_strconcat(
"There are several template files in this directory. For these templates you can use wildcards.\n\
For more information read the documentation (in ", app->docdir, DIR_SEP "index.html or visit " GEANY_HOMEPAGE ").",
					NULL);
			utils_write_file(templates_readme, text);
			g_free(text);
		}
		g_free(templates_readme);
	}

	g_free(filedefs_dir);
	g_free(templates_dir);
	g_free(conf_file);

	return saved_errno;
}


/* Replaces all occurrences of needle in haystack with replacement.
 * New code should use utils_string_replace_all() instead.
 * All strings have to be NULL-terminated and needle and replacement have to be different,
 * e.g. needle "%" and replacement "%%" causes an endless loop */
gchar *utils_str_replace(gchar *haystack, const gchar *needle, const gchar *replacement)
{
	gint i;
	gchar *start;
	gint lt_pos;
	gchar *result;
	GString *str;

	if (haystack == NULL)
		return NULL;

	if (needle == NULL || replacement == NULL)
		return haystack;

	if (utils_str_equal(needle, replacement))
		return haystack;

	start = strstr(haystack, needle);
	lt_pos = utils_strpos(haystack, needle);

	if (start == NULL || lt_pos == -1)
		return haystack;

	/* substitute by copying */
	str = g_string_sized_new(strlen(haystack));
	for (i = 0; i < lt_pos; i++)
	{
		g_string_append_c(str, haystack[i]);
	}
	g_string_append(str, replacement);
	g_string_append(str, haystack + lt_pos + strlen(needle));

	result = str->str;
	g_free(haystack);
	g_string_free(str, FALSE);
	return utils_str_replace(result, needle, replacement);
}


gint utils_strpos(const gchar *haystack, const gchar *needle)
{
	gint haystack_length = strlen(haystack);
	gint needle_length = strlen(needle);
	gint i, j, pos = -1;

	if (needle_length > haystack_length)
	{
		return -1;
	}
	else
	{
		for (i = 0; (i < haystack_length) && pos == -1; i++)
		{
			if (haystack[i] == needle[0] && needle_length == 1)	return i;
			else if (haystack[i] == needle[0])
			{
				for (j = 1; (j < needle_length); j++)
				{
					if (haystack[i+j] == needle[j])
					{
						if (pos == -1) pos = i;
					}
					else
					{
						pos = -1;
						break;
					}
				}
			}
		}
		return pos;
	}
}


gchar *utils_get_date_time(const gchar *format, time_t *time_to_use)
{
	time_t tp;
	const struct tm *tm;
	gchar *date;

	if (format == NULL)
		return NULL;

	if (time_to_use != NULL)
		tp = *time_to_use;
	else
		tp = time(NULL);

	tm = localtime(&tp);
	date = g_malloc0(256);
	strftime(date, 256, format, tm);
	return date;
}


gchar *utils_get_initials(const gchar *name)
{
	gint i = 1, j = 1;
	gchar *initials = g_malloc0(5);

	initials[0] = name[0];
	while (name[i] != '\0' && j < 4)
	{
		if (name[i] == ' ' && name[i + 1] != ' ')
		{
			initials[j++] = name[i + 1];
		}
		i++;
	}
	return initials;
}


/**
 *  Convenience function for g_key_file_get_integer() to add a default value argument.
 *
 *  @param config A GKeyFile object.
 *  @param section The group name to look in for the key.
 *  @param key The key to find.
 *  @param default_value The default value which will be returned when @c section or @c key
 *         don't exist.
 *
 *  @return The value associated with c key as an integer, or the given default value if the value
 *          could not be retrieved.
 **/
gint utils_get_setting_integer(GKeyFile *config, const gchar *section, const gchar *key,
							   const gint default_value)
{
	gint tmp;
	GError *error = NULL;

	if (config == NULL) return default_value;

	tmp = g_key_file_get_integer(config, section, key, &error);
	if (error)
	{
		g_error_free(error);
		return default_value;
	}
	return tmp;
}


/**
 *  Convenience function for g_key_file_get_boolean() to add a default value argument.
 *
 *  @param config A GKeyFile object.
 *  @param section The group name to look in for the key.
 *  @param key The key to find.
 *  @param default_value The default value which will be returned when @c section or @c key
 *         don't exist.
 *
 *  @return The value associated with c key as a boolean, or the given default value if the value
 *          could not be retrieved.
 **/
gboolean utils_get_setting_boolean(GKeyFile *config, const gchar *section, const gchar *key,
								   const gboolean default_value)
{
	gboolean tmp;
	GError *error = NULL;

	if (config == NULL) return default_value;

	tmp = g_key_file_get_boolean(config, section, key, &error);
	if (error)
	{
		g_error_free(error);
		return default_value;
	}
	return tmp;
}


/**
 *  Convenience function for g_key_file_get_string() to add a default value argument.
 *
 *  @param config A GKeyFile object.
 *  @param section The group name to look in for the key.
 *  @param key The key to find.
 *  @param default_value The default value which will be returned when @c section or @c key
 *         don't exist.
 *
 *  @return A newly allocated string, or the given default value if the value could not be
 *          retrieved.
 **/
gchar *utils_get_setting_string(GKeyFile *config, const gchar *section, const gchar *key,
								const gchar *default_value)
{
	gchar *tmp;
	GError *error = NULL;

	if (config == NULL) return g_strdup(default_value);

	tmp = g_key_file_get_string(config, section, key, &error);
	if (error)
	{
		g_error_free(error);
		return (gchar*) g_strdup(default_value);
	}
	return tmp;
}


void utils_switch_document(gint direction)
{
	gint page_count = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
	gint cur_page = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));

	if (direction == LEFT)
	{
		if (cur_page > 0)
			gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), cur_page - 1);
		else
			gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page_count - 1);
	}
	else if (direction == RIGHT)
	{
		if (cur_page < page_count - 1)
			gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), cur_page + 1);
		else
			gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), 0);
	}
}


void utils_replace_filename(gint idx)
{
	gchar *filebase;
	gchar *filename;
	struct TextToFind ttf;

	if (idx == -1 || doc_list[idx].file_type == NULL) return;

	filebase = g_strconcat(GEANY_STRING_UNTITLED, ".", (doc_list[idx].file_type)->extension, NULL);
	filename = g_path_get_basename(doc_list[idx].file_name);

	/* only search the first 3 lines */
	ttf.chrg.cpMin = 0;
	ttf.chrg.cpMax = sci_get_position_from_line(doc_list[idx].sci, 3);
	ttf.lpstrText = (gchar*)filebase;

	if (sci_find_text(doc_list[idx].sci, SCFIND_MATCHCASE, &ttf) != -1)
	{
		sci_target_start(doc_list[idx].sci, ttf.chrgText.cpMin);
		sci_target_end(doc_list[idx].sci, ttf.chrgText.cpMax);
		sci_target_replace(doc_list[idx].sci, filename, FALSE);
	}

	g_free(filebase);
	g_free(filename);
}


gchar *utils_get_hex_from_color(GdkColor *color)
{
	gchar *buffer = g_malloc0(9);

	if (color == NULL) return NULL;

	g_snprintf(buffer, 8, "#%02X%02X%02X",
	      (guint) (utils_scale_round(color->red / 256, 255)),
	      (guint) (utils_scale_round(color->green / 256, 255)),
	      (guint) (utils_scale_round(color->blue / 256, 255)));

	return buffer;
}


/* Get directory from current file in the notebook.
 * Returns dir string that should be freed or NULL, depending on whether current file is valid.
 * Returned string is in UTF-8 encoding */
gchar *utils_get_current_file_dir_utf8(void)
{
	gint cur_idx = document_get_cur_idx();

	if (DOC_IDX_VALID(cur_idx)) /* if valid page found */
	{
		/* get current filename */
		const gchar *cur_fname = doc_list[cur_idx].file_name;

		if (cur_fname != NULL)
		{
			/* get folder part from current filename */
			return g_path_get_dirname(cur_fname); /* returns "." if no path */
		}
	}

	return NULL; /* no file open */
}


/* very simple convenience function */
void utils_beep(void)
{
	if (prefs.beep_on_errors) gdk_beep();
}


/* taken from busybox, thanks */
gchar *utils_make_human_readable_str(guint64 size, gulong block_size,
									 gulong display_unit)
{
	/* The code will adjust for additional (appended) units. */
	static const gchar zero_and_units[] = { '0', 0, 'K', 'M', 'G', 'T' };
	static const gchar fmt[] = "%Lu %c%c";
	static const gchar fmt_tenths[] = "%Lu.%d %c%c";

	guint64 val;
	gint frac;
	const gchar *u;
	const gchar *f;

	u = zero_and_units;
	f = fmt;
	frac = 0;

	val = size * block_size;
	if (val == 0) return g_strdup(u);

	if (display_unit)
	{
		val += display_unit/2;	/* Deal with rounding. */
		val /= display_unit;	/* Don't combine with the line above!!! */
	}
	else
	{
		++u;
		while ((val >= KILOBYTE) && (u < zero_and_units + sizeof(zero_and_units) - 1))
		{
			f = fmt_tenths;
			++u;
			frac = ((((gint)(val % KILOBYTE)) * 10) + (KILOBYTE/2)) / KILOBYTE;
			val /= KILOBYTE;
		}
		if (frac >= 10)
		{		/* We need to round up here. */
			++val;
			frac = 0;
		}
	}

	/* If f==fmt then 'frac' and 'u' are ignored. */
	return g_strdup_printf(f, val, frac, *u, 'b');
}


 guint utils_get_value_of_hex(const gchar ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	else if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	else if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	else
		return 0;
}


/* utils_strtod() converts a string containing a hex colour ("0x00ff00") into an integer.
 * Basically, it is the same as strtod() would do, but it does not understand hex colour values,
 * before ANSI-C99. With with_route set, it takes strings of the format "#00ff00". */
gint utils_strtod(const gchar *source, gchar **end, gboolean with_route)
{
	guint red, green, blue, offset = 0;

	if (source == NULL)
		return -1;
	else if (with_route && (strlen(source) != 7 || source[0] != '#'))
		return -1;
	else if (! with_route && (strlen(source) != 8 || source[0] != '0' ||
		(source[1] != 'x' && source[1] != 'X')))
	{
		return -1;
	}

	/* offset is set to 1 when the string starts with 0x, otherwise it starts with #
	 * and we don't need to increase the index */
	if (! with_route)
		offset = 1;

	red = utils_get_value_of_hex(
					source[1 + offset]) * 16 + utils_get_value_of_hex(source[2 + offset]);
	green = utils_get_value_of_hex(
					source[3 + offset]) * 16 + utils_get_value_of_hex(source[4 + offset]);
	blue = utils_get_value_of_hex(
					source[5 + offset]) * 16 + utils_get_value_of_hex(source[6 + offset]);

	return (red | (green << 8) | (blue << 16));
}


/* Returns: newly allocated string with the current time formatted HH:MM:SS. */
gchar *utils_get_current_time_string(void)
{
	const time_t tp = time(NULL);
	const struct tm *tmval = localtime(&tp);
	gchar *result = g_malloc0(9);

	strftime(result, 9, "%H:%M:%S", tmval);
	result[8] = '\0';
	return result;
}


GIOChannel *utils_set_up_io_channel(
				gint fd, GIOCondition cond, gboolean nblock, GIOFunc func, gpointer data)
{
	GIOChannel *ioc;
	/*const gchar *encoding;*/

	#ifdef G_OS_WIN32
	ioc = g_io_channel_win32_new_fd(fd);
	#else
	ioc = g_io_channel_unix_new(fd);
	#endif

	if (nblock)
		g_io_channel_set_flags(ioc, G_IO_FLAG_NONBLOCK, NULL);

	g_io_channel_set_encoding(ioc, NULL, NULL);
/*
	if (! g_get_charset(&encoding))
	{	// hope this works reliably
		GError *error = NULL;
		g_io_channel_set_encoding(ioc, encoding, &error);
		if (error)
		{
			geany_debug("%s: %s", __func__, error->message);
			g_error_free(error);
			return ioc;
		}
	}
*/
	/* "auto-close" ;-) */
	g_io_channel_set_close_on_unref(ioc, TRUE);

	g_io_add_watch(ioc, cond, func, data);
	g_io_channel_unref(ioc);

	return ioc;
}


gchar **utils_read_file_in_array(const gchar *filename)
{
	gchar **result = NULL;
	gchar *data;

	if (filename == NULL) return NULL;

	g_file_get_contents(filename, &data, NULL, NULL);

	if (data != NULL)
	{
		result = g_strsplit_set(data, "\r\n", -1);
	}

	return result;
}


/* Contributed by Stefan Oltmanns, thanks.
 * Replaces \\, \r, \n, \t and \uXXX by their real counterparts */
gboolean utils_str_replace_escape(gchar *string)
{
	gsize i, j;
	guint unicodechar;

	j = 0;
	for (i = 0; i < strlen(string); i++)
	{
		if (string[i]=='\\')
		{
			if (i++ >= strlen(string))
			{
				return FALSE;
			}
			switch (string[i])
			{
				case '\\':
					string[j] = '\\';
					break;
				case 'n':
					string[j] = '\n';
					break;
				case 'r':
					string[j] = '\r';
					break;
				case 't':
					string[j] = '\t';
					break;
#if 0
				case 'x': /* Warning: May produce illegal utf-8 string! */
					i += 2;
					if (i >= strlen(string))
					{
						return FALSE;
					}
					if (isdigit(string[i-1])) string[j] = string[i-1]-48;
					else if (isxdigit(string[i-1])) string[j] = tolower(string[i-1])-87;
					else return FALSE;
					string[j] <<= 4;
					if (isdigit(string[i])) string[j] |= string[i]-48;
					else if (isxdigit(string[i])) string[j] |= tolower(string[i])-87;
					else return FALSE;
					break;
#endif
				case 'u':
					i += 2;
					if (i >= strlen(string))
					{
						return FALSE;
					}
					if (isdigit(string[i-1])) unicodechar = string[i-1]-48;
					else if (isxdigit(string[i-1])) unicodechar = tolower(string[i-1])-87;
					else return FALSE;
					unicodechar <<= 4;
					if (isdigit(string[i])) unicodechar |= string[i]-48;
					else if (isxdigit(string[i])) unicodechar |= tolower(string[i])-87;
					else return FALSE;
					if (((i+2) < strlen(string)) && (isdigit(string[i+1]) || isxdigit(string[i+1]))
						&& (isdigit(string[i+2]) || isxdigit(string[i+2])))
					{
						i += 2;
						unicodechar <<= 8;
						if (isdigit(string[i-1])) unicodechar |= ((string[i-1]-48)<<4);
						else unicodechar |= ((tolower(string[i-1])-87) << 4);
						if (isdigit(string[i])) unicodechar |= string[i]-48;
						else unicodechar |= tolower(string[i])-87;
					}
					if (((i+2) < strlen(string)) && (isdigit(string[i+1]) || isxdigit(string[i+1]))
						&& (isdigit(string[i+2]) || isxdigit(string[i+2])))
					{
						i += 2;
						unicodechar <<= 8;
						if (isdigit(string[i-1])) unicodechar |= ((string[i-1]-48) << 4);
						else unicodechar |= ((tolower(string[i-1])-87) << 4);
						if (isdigit(string[i])) unicodechar |= string[i]-48;
						else unicodechar |= tolower(string[i])-87;
					}
					if(unicodechar < 0x80)
					{
						string[j] = unicodechar;
					}
					else if (unicodechar < 0x800)
					{
						string[j] = (unsigned char) ((unicodechar >> 6)| 0xC0);
						j++;
						string[j] = (unsigned char) ((unicodechar & 0x3F)| 0x80);
					}
					else if (unicodechar < 0x10000)
					{
						string[j] = (unsigned char) ((unicodechar >> 12) | 0xE0);
						j++;
						string[j] = (unsigned char) (((unicodechar >> 6) & 0x3F) | 0x80);
						j++;
						string[j] = (unsigned char) ((unicodechar & 0x3F) | 0x80);
					}
					else if (unicodechar < 0x110000) /* more chars are not allowed in unicode */
					{
						string[j] = (unsigned char) ((unicodechar >> 18) | 0xF0);
						j++;
						string[j] = (unsigned char) (((unicodechar >> 12) & 0x3F) | 0x80);
						j++;
						string[j] = (unsigned char) (((unicodechar >> 6) & 0x3F) | 0x80);
						j++;
						string[j] = (unsigned char) ((unicodechar & 0x3F) | 0x80);
					}
					else
					{
						return FALSE;
					}
					break;
				default:
					return FALSE;
			}
		}
		else
		{
			string[j] = string[i];
		}
		j++;
	}
	while (j < i)
	{
		string[j] = 0;
		j++;
	}
	return TRUE;
}


/* Wraps a string in place, replacing a space with a newline character.
 * wrapstart is the minimum position to start wrapping or -1 for default */
gboolean utils_wrap_string(gchar *string, gint wrapstart)
{
	gchar *pos, *linestart;
	gboolean ret = FALSE;

	if (wrapstart < 0) wrapstart = 80;

	for (pos = linestart = string; *pos != '\0'; pos++)
	{
		if (pos - linestart >= wrapstart && *pos == ' ')
		{
			*pos = '\n';
			linestart = pos;
			ret = TRUE;
		}
	}
	return ret;
}


/**
 *  Converts the given UTF-8 encoded string into locale encoding.
 *  On Windows platforms, it does nothing and instead it just returns a copy of the input string.
 *
 *  @param utf8_text UTF-8 encoded text.
 *
 *  @return The converted string in locale encoding, or a copy of the input string if conversion
 *    failed. Should be freed with g_free(). If @a utf8_text is @c NULL, @c NULL is returned.
 **/
gchar *utils_get_locale_from_utf8(const gchar *utf8_text)
{
#ifdef G_OS_WIN32
	/* just do nothing on Windows platforms, this ifdef is just to prevent unwanted conversions
	 * which would result in wrongly converted strings */
	return g_strdup(utf8_text);
#else
	gchar *locale_text;

	if (! utf8_text)
		return NULL;
	locale_text = g_locale_from_utf8(utf8_text, -1, NULL, NULL, NULL);
	if (locale_text == NULL)
		locale_text = g_strdup(utf8_text);
	return locale_text;
#endif
}


/**
 *  Converts the given string (in locale encoding) into UTF-8 encoding.
 *  On Windows platforms, it does nothing and instead it just returns a copy of the input string.
 *
 *  @param locale_text Text in locale encoding.
 *
 *  @return The converted string in UTF-8 encoding, or a copy of the input string if conversion
 *    failed. Should be freed with g_free(). If @a locale_text is @c NULL, @c NULL is returned.
 **/
gchar *utils_get_utf8_from_locale(const gchar *locale_text)
{
#ifdef G_OS_WIN32
	/* just do nothing on Windows platforms, this ifdef is just to prevent unwanted conversions
	 * which would result in wrongly converted strings */
	return g_strdup(locale_text);
#else
	gchar *utf8_text;

	if (! locale_text)
		return NULL;
	utf8_text = g_locale_to_utf8(locale_text, -1, NULL, NULL, NULL);
	if (utf8_text == NULL)
		utf8_text = g_strdup(locale_text);
	return utf8_text;
#endif
}


/* Frees all passed pointers if they are *ALL* non-NULL.
 * Do not use if any pointers may be NULL.
 * The first argument is nothing special, it will also be freed.
 * The list must be ended with NULL. */
void utils_free_pointers(gpointer first, ...)
{
	va_list a;
	gpointer sa;

    for (va_start(a, first);  (sa = va_arg(a, gpointer), sa!=NULL);)
    {
    	if (sa != NULL)
    		g_free(sa);
	}
	va_end(a);

    if (first != NULL)
    	g_free(first);
}


/* Creates a string array deep copy of a series of non-NULL strings.
 * The first argument is nothing special.
 * The list must be ended with NULL.
 * If first is NULL, NULL is returned. */
gchar **utils_strv_new(const gchar *first, ...)
{
	gsize strvlen, i;
	va_list args;
	gchar *str;
	gchar **strv;

	if (first == NULL)
		return NULL;

	strvlen = 1;	/* for first argument */

    /* count other arguments */
    va_start(args, first);
    for (; va_arg(args, gchar*) != NULL; strvlen++);
	va_end(args);

	strv = g_new(gchar*, strvlen + 1);	/* +1 for NULL terminator */
	strv[0] = g_strdup(first);

    va_start(args, first);
    for (i = 1; str = va_arg(args, gchar*), str != NULL; i++)
    {
		strv[i] = g_strdup(str);
	}
	va_end(args);

	strv[i] = NULL;
	return strv;
}


#if ! GLIB_CHECK_VERSION(2, 8, 0)
/* Taken from GLib SVN, 2007-03-10 */
/**
 * g_mkdir_with_parents:
 * @pathname: a pathname in the GLib file name encoding
 * @mode: permissions to use for newly created directories
 *
 * Create a directory if it doesn't already exist. Create intermediate
 * parent directories as needed, too.
 *
 * Returns: 0 if the directory already exists, or was successfully
 * created. Returns -1 if an error occurred, with errno set.
 *
 * Since: 2.8
 */
int
g_mkdir_with_parents (const gchar *pathname,
		      int          mode)
{
  gchar *fn, *p;

  if (pathname == NULL || *pathname == '\0')
    {
      errno = EINVAL;
      return -1;
    }

  fn = g_strdup (pathname);

  if (g_path_is_absolute (fn))
    p = (gchar *) g_path_skip_root (fn);
  else
    p = fn;

  do
    {
      while (*p && !G_IS_DIR_SEPARATOR (*p))
	p++;

      if (!*p)
	p = NULL;
      else
	*p = '\0';

      if (!g_file_test (fn, G_FILE_TEST_EXISTS))
	{
	  if (g_mkdir (fn, mode) == -1)
	    {
	      int errno_save = errno;
	      g_free (fn);
	      errno = errno_save;
	      return -1;
	    }
	}
      else if (!g_file_test (fn, G_FILE_TEST_IS_DIR))
	{
	  g_free (fn);
	  errno = ENOTDIR;
	  return -1;
	}
      if (p)
	{
	  *p++ = G_DIR_SEPARATOR;
	  while (*p && G_IS_DIR_SEPARATOR (*p))
	    p++;
	}
    }
  while (p);

  g_free (fn);

  return 0;
}
#endif


/**
 *  Create a directory if it doesn't already exist.
 *  Create intermediate parent directories as needed, too.
 *
 *  @param path The path of the directory to create, in locale encoding.
 *  @param create_parent_dirs Whether to create intermediate parent directories if necessary.
 *
 *  @return 0 if the directory was successfully created, otherwise the @c errno of the
 *          failed operation is returned.
 **/
gint utils_mkdir(const gchar *path, gboolean create_parent_dirs)
{
	gint mode = 0700;
	gint result;

	if (path == NULL || strlen(path) == 0)
		return EFAULT;

	result = (create_parent_dirs) ? g_mkdir_with_parents(path, mode) : g_mkdir(path, mode);
	if (result != 0)
		return errno;
	return 0;
}


/**
 *  Gets a sorted list of files from the specified directory.
 *  Locale encoding is expected for path and used for the file list. The list and the data
 *  in the list should be freed after use.
 *
 *  @param path The path of the directory to scan, in locale encoding.
 *  @param length The location to store the number of non-@a NULL data items in the list,
 *                unless @a NULL.
 *  @param error The is the location for storing a possible error, or @a NULL.
 *
 *  @return A newly allocated list or @a NULL if no files found. The list and its data should be
 *          freed when no longer needed.
 **/
GSList *utils_get_file_list(const gchar *path, guint *length, GError **error)
{
	GSList *list = NULL;
	guint len = 0;
	GDir *dir;

	if (error)
		*error = NULL;
	if (length)
		*length = 0;
	g_return_val_if_fail(path != NULL, NULL);

	dir = g_dir_open(path, 0, error);
	if (dir == NULL)
		return NULL;

	while (1)
	{
		const gchar *filename = g_dir_read_name(dir);
		if (filename == NULL) break;

		list = g_slist_insert_sorted(list, g_strdup(filename), (GCompareFunc) strcmp);
		len++;
	}
	g_dir_close(dir);

	if (length)
		*length = len;
	return list;
}


/* returns TRUE if any letter in str is a capital, FALSE otherwise. Should be Unicode safe. */
gboolean utils_str_has_upper(const gchar *str)
{
	gunichar c;

	if (str == NULL || *str == '\0' || ! g_utf8_validate(str, -1, NULL))
		return FALSE;

	while (*str != '\0')
	{
		c = g_utf8_get_char(str);
		/* check only letters and stop once the first non-capital was found */
		if (g_unichar_isalpha(c) && g_unichar_isupper(c))
			return TRUE;
		/* FIXME don't write a const string */
		str = g_utf8_next_char(str);
	}
	return FALSE;
}


/**
 *  Replaces all occurrences of @c needle in @c haystack with @c replace.
 *  Currently this is not safe if @c replace matches @c needle, so @c needle and @c replace
 *  must not be equal.
 *
 *  @param haystack The input string to operate on. This string is modified in place.
 *  @param needle The string which should be replaced.
 *  @param replace The replacement for @c needle.
 *
 *  @return @a TRUE if @c needle was found, else @a FALSE.
 **/
gboolean utils_string_replace_all(GString *haystack, const gchar *needle, const gchar *replace)
{
	const gchar *c;
	gssize pos = -1;

	g_return_val_if_fail(NZV(needle), FALSE);
	g_return_val_if_fail(! NZV(replace) || strstr(replace, needle) == NULL, FALSE);

	while (1)
	{
		c = strstr(haystack->str, needle);
		if (c == NULL)
			break;
		else
		{
			pos = c - haystack->str;
			g_string_erase(haystack, pos, strlen(needle));
			if (replace)
				g_string_insert(haystack, pos, replace);
		}
	}
	return (pos != -1);
}


/* Get project or default startup directory (if set), or NULL. */
const gchar *utils_get_default_dir_utf8(void)
{
	if (app->project && NZV(app->project->base_path))
	{
		return app->project->base_path;
	}

	if (NZV(prefs.default_open_path))
	{
		return prefs.default_open_path;
	}
	return NULL;
}


static gboolean check_error(GError **error)
{
	if (error != NULL && *error != NULL)
	{
		/* imitate the GLib warning */
		g_warning(
			"GError set over the top of a previous GError or uninitialized memory.\n"
			"This indicates a bug in someone's code. You must ensure an error is NULL "
			"before it's set.");
		/* after returning the code may segfault, but we don't care because we should
		 * make sure *error is NULL */
		return FALSE;
	}
	return TRUE;
}


/**
 *  This is a wrapper function for g_spawn_sync() and internally calls this function on Unix-like
 *  systems. On Win32 platforms, it uses the Windows API.
 *
 *  @param dir The child's current working directory, or @a NULL to inherit parent's.
 *  @param argv The child's argument vector.
 *  @param env The child's environment, or @a NULL to inherit parent's.
 *  @param flags Flags from GSpawnFlags.
 *  @param child_setup A function to run in the child just before exec().
 *  @param user_data The user data for child_setup.
 *  @param std_out The return location for child output.
 *  @param std_err The return location for child error messages.
 *  @param exit_status The child exit status, as returned by waitpid().
 *  @param error The return location for error or @a NULL.
 *
 *  @return @a TRUE on success, @a FALSE if an error was set.
 **/
gboolean utils_spawn_sync(const gchar *dir, gchar **argv, gchar **env, GSpawnFlags flags,
						  GSpawnChildSetupFunc child_setup, gpointer user_data, gchar **std_out,
						  gchar **std_err, gint *exit_status, GError **error)
{
	gboolean result;

	if (! check_error(error))
		return FALSE;

	if (argv == NULL)
	{
		*error = g_error_new(G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "argv must not be NULL");
		return FALSE;
	}

	if (std_out)
		*std_out = NULL;

	if (std_err)
		*std_err = NULL;

#ifdef G_OS_WIN32
	result = win32_spawn(dir, argv, env, flags, std_out, std_err, exit_status, error);
#else
	result = g_spawn_sync(dir, argv, env, flags, NULL, NULL, std_out, std_err, exit_status, error);
#endif

	return result;
}


/**
 *  This is a wrapper function for g_spawn_async() and internally calls this function on Unix-like
 *  systems. On Win32 platforms, it uses the Windows API.
 *
 *  @param dir The child's current working directory, or @a NULL to inherit parent's.
 *  @param argv The child's argument vector.
 *  @param env The child's environment, or @a NULL to inherit parent's.
 *  @param flags Flags from GSpawnFlags.
 *  @param child_setup A function to run in the child just before exec().
 *  @param user_data The user data for child_setup.
 *  @param child_pid The return location for child process ID, or NULL.
 *  @param error The return location for error or @a NULL.
 *
 *  @return @a TRUE on success, @a FALSE if an error was set.
 **/
gboolean utils_spawn_async(const gchar *dir, gchar **argv, gchar **env, GSpawnFlags flags,
						   GSpawnChildSetupFunc child_setup, gpointer user_data, GPid *child_pid,
						   GError **error)
{
	gboolean result;

	if (! check_error(error))
		return FALSE;

	if (argv == NULL)
	{
		*error = g_error_new(G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, "argv must not be NULL");
		return FALSE;
	}

#ifdef G_OS_WIN32
	result = win32_spawn(dir, argv, env, flags, NULL, NULL, NULL, error);
#else
	result = g_spawn_async(dir, argv, env, flags, NULL, NULL, child_pid, error);
#endif
	return result;
}
