/** @file test.c
 *  Unit testing driver.
 */

/*
 * Copyright (C) 2006 Philip Lewis <pcl@pclewis.com>
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 * 
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

/****************************************************************************/
/****************************************************************************/

/* compatibility stuff - must come first */
#include "compat.h"

#include <glib.h>
#include <glib/gstdio.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <errno.h>
#include "misc.h"
#include "test.h"
#include "test-suites.h"

/****************************************************************************/
/****************************************************************************/

#ifdef USE_COLOR
  #define ESC         "\033"
  #define COLOR(c)    ESC "[3" # c "m"
  #define COLOR_BOLD  ESC "[1m"
  #define COLOR_RESET ESC "[0m"
#else
  #define COLOR(c) ""
  #define COLOR_BOLD ""
  #define COLOR_RESET ""
#endif

/****************************************************************************/
/****************************************************************************/

gint                         test_assertion_count       = 0;
gint                         test_assertion_failed      = 0;
static gboolean              verbose                    = FALSE;
static gboolean              quiet                      = FALSE;
static gchar                *current_suite              = NULL;
static gchar                *current_test               = NULL;
gboolean                     test_last_assertion_result = FALSE;
TestResult                   test_current_result        = TEST_RESULT_FAIL;
static TestSetupFunction     setup_func                 = NULL;
static TestTearDownFunction  teardown_func              = NULL;
static gchar                *status_line_ptr            = NULL;
static gchar                 status_line[1024+1];
static gchar                *status_line_end            = status_line+1024;

/** fd for test_temp_file_setup */
int                          test_temp_fd               = -1;
static gchar                *test_temp_name             = NULL;

/** Hash table mapping location strings to last status */
static GHashTable           *assertion_locations        = NULL;
#define LAST_STATUS_SUCCESS GINT_TO_POINTER( 1)
#define LAST_STATUS_FAILURE GINT_TO_POINTER(-1)


static void suite_header();
static void test_header();

/****************************************************************************/
/****************************************************************************/

/** Test driver command line options */
static GOptionEntry entries[] = {
  { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
    "Show individual tests.", NULL },
  { "quiet",   'q', 0, G_OPTION_ARG_NONE, &quiet,
    "Don't output anything (overrides --verbose).", NULL },
  { NULL, 0, 0, 0, NULL, NULL, NULL }
};

/****************************************************************************/
/****************************************************************************/

/** Quiet handler for g_print messages when --quiet is specified. */
static void
quiet_print_handler( const gchar UNUSED(*message) )
{
  return;
}

/** Quiet handler for non-critical log messages when --quiet is specified. */
static void
quiet_log_handler(const gchar     UNUSED(*log_domain),
                  GLogLevelFlags  UNUSED(log_level),
                  const gchar     UNUSED(*message),
                  gpointer        UNUSED(user_data))
{
  return;
}

/****************************************************************************/
/****************************************************************************/

/** Normal log handler */
static void
normal_log_handler(const gchar     *log_domain,
                   GLogLevelFlags   log_level,
                   const gchar     *message,
                   gpointer         user_data)
{
  /* skip MESSAGE and DEBUG messages unless we're being verbose */
  if ( !verbose && (log_level & G_LOG_LEVEL_MESSAGE ||
                    log_level & G_LOG_LEVEL_DEBUG) )
    return;
  if ( current_suite != NULL ) {
    if ( verbose && current_test != NULL )
      g_print("\r - (" COLOR(3) COLOR_BOLD "----" COLOR_RESET ")");
    else if ( !verbose )
      g_print("\r[" COLOR(3) COLOR_BOLD "----" COLOR_RESET "]");
  }
  g_print( COLOR(3) );
#if WIN32
  if (verbose) {
    DWORD last_error = GetLastError();
    if ( last_error != 0 ) {
      gchar *err = g_win32_error_message( last_error );
      gchar *msg = g_strdup_printf("%s\n\tLast win32 error: %s",message,err);
      g_log_default_handler( log_domain, log_level, msg, user_data );
      g_free(err);
      g_free(msg);
    }
  } else {
    g_log_default_handler( log_domain, log_level, message, user_data );
  }
#else /* !WIN32 */
  g_log_default_handler( log_domain, log_level, message, user_data );
#endif /* WIN32 */
  g_print( COLOR_RESET );
  if ( current_suite != NULL ) {
    if ( verbose && current_test != NULL )
      test_header();
    else if ( !verbose )
      suite_header();
  }
  return;
}

/****************************************************************************/
/****************************************************************************/

/** Callback function for after our options have been parsed. */
static gboolean
test_post_parse(GOptionContext UNUSED(*context),
                GOptionGroup UNUSED(*group),
                gpointer UNUSED(data),
                GError UNUSED(**error))
{
  if ( quiet ) {
    g_set_print_handler( &quiet_print_handler );
    g_log_set_handler(NULL, G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO |
                      G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_WARNING,
                      quiet_log_handler, NULL);
    g_log_set_default_handler( quiet_log_handler, NULL );
  } else {
    g_log_set_default_handler( normal_log_handler, NULL );
  }
  return TRUE;
}

/****************************************************************************/
/****************************************************************************/

/* Create and return an option group with test driver options. */
GOptionGroup
*test_get_option_group()
{
  GOptionGroup    *group   = NULL;

  group = g_option_group_new("test", "Unit Testing Options:",
                             "Show unit testing options", NULL, NULL );
  g_option_group_add_entries( group, entries );
  g_option_group_set_parse_hooks( group, NULL, test_post_parse );
  return group;
}

/****************************************************************************/
/****************************************************************************/

/* Run all tests */
int
test_main()
{
  /* disable output buffer */
  setvbuf( stdout, (char *)NULL, _IONBF, 0 );

  /* create hash table for assertions */
  assertion_locations = g_hash_table_new( g_str_hash, g_str_equal );

  run_suites();

  g_hash_table_destroy( assertion_locations );

  g_print("Assertions passed: %d/%d\n",
          test_assertion_count - test_assertion_failed,
          test_assertion_count );

  return test_assertion_failed;
}

/****************************************************************************/
/****************************************************************************/

/** Clear status line */
static void
reset_status_line()
{
  memset( status_line, 0, sizeof(status_line) );
  status_line_ptr = status_line;
}

/****************************************************************************/
/****************************************************************************/

/** Print out header of current suite. */
static void
suite_header()
{
  if ( verbose )
    g_print( "\r" COLOR_BOLD "====== %s ======" COLOR_RESET, current_suite );
  else
    g_print( "\r[%4s] %30s %s", "", current_suite, status_line );
}

/** Print out header of current test. */
static void
test_header()
{
  g_print( "\r - (%4s) %40s %s", "", current_test, status_line );
}

/****************************************************************************/
/****************************************************************************/

gboolean
test_assert( gboolean condition, gchar *description, gchar *location )
{
  gpointer last_result = NULL;

  test_last_assertion_result = condition;

  last_result =  g_hash_table_lookup( assertion_locations, location );

  if ( last_result == NULL ) { /* first time running this test */
    ++test_assertion_count;
    g_hash_table_insert( assertion_locations, location,
                         condition ? LAST_STATUS_SUCCESS
                                   : LAST_STATUS_FAILURE );
  } else {
    /* if we've already done this assertion once, we don't want to count it
     * again unless it was previously successful and is now a failure */
    if ( last_result == LAST_STATUS_SUCCESS && !condition ) {
      g_hash_table_replace( assertion_locations, location,
                            LAST_STATUS_FAILURE );
    } else {
      return condition;
    }
  }

  if ( condition ) {
    if ( verbose ) {
      g_print(".");
      *status_line_ptr++ = '.';
      if ( status_line_ptr >= status_line_end ) {
        g_print("\n");
        reset_status_line();
        test_header();
      }
    }
  } else {
    ++test_assertion_failed;
    g_print( "\rAssertion failed: %s   \n", description );
    g_print( "\tin suite \"%s\", test %s\n",
              current_suite, current_test );
    g_print( "\tat %s\n", location );
    if ( verbose ) {
      *status_line_ptr++ = 'F';
      if ( status_line_ptr >= status_line_end ) {
        g_print("\n");
        reset_status_line();
        test_header();
      }
      test_header();
    } else {
      suite_header();
    }
  }

  return condition;
}

/****************************************************************************/
/****************************************************************************/

gboolean
test_assert_format( gboolean condition, gchar *location, gchar *format, ... )
{
  va_list  arguments;
  gchar   *description;
  gboolean result;

  va_start( arguments, format );

  description = g_strdup_vprintf( format, arguments );
  result = test_assert( condition, description, location );
  g_free(description);

  va_end( arguments );

  return result;
}

/****************************************************************************/
/****************************************************************************/

gboolean
test_assert_op_format( gboolean condition,   gchar *operation,
                       gchar *left,          gchar *format_left,
                       gchar *right,         gchar *format_right,
                       gchar *location,      ... )
{
  va_list  arguments;
  gchar   *format;
  gchar   *description;
  gboolean result;

  va_start( arguments, location );

  format = g_strdup_printf( "%s %s %s (%s %s %s)",
                            left,        operation, right,
                            format_left, operation, format_right );
  description = g_strdup_vprintf( format, arguments );

  result = test_assert( condition, description, location );

  g_free(format);
  g_free(description);

  va_end( arguments );

  return result;
}

/****************************************************************************/
/****************************************************************************/

void
test_run_test( TestFunction test, gchar *title )
{
  /* keep track previous failures */
  int prev_failures = test_assertion_failed;
  TestResult result = TEST_RESULT_ERROR;

  /* clear out assertion location info */
  g_hash_table_remove_all( assertion_locations );

  if ( setup_func != NULL )
    setup_func();

  /* clear errno */
  if ( errno != 0 ) {
    g_log( G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
           "clearing errno before test `%s' (%d: %s)",
           title, errno, g_strerror(errno) );
    errno = 0;
  }

  current_test = title;
  if ( verbose )
    test_header();

  /* run the test */
  result = test();

  if ( result == TEST_RESULT_DONE &&
       prev_failures == test_assertion_failed ) {
    if ( verbose ) {
      g_print(" PASS\r - (" COLOR(2) COLOR_BOLD "PASS" COLOR_RESET ")\n");
    } else {
      g_print(".");
      *status_line_ptr++ = '.';
      if ( status_line_ptr >= status_line_end ) {
        g_print("\n");
        reset_status_line();
        test_header();
      }
    }
  } else {
    if ( verbose ) {
      g_print(" FAIL\r - (" COLOR(1) COLOR_BOLD "FAIL" COLOR_RESET ")\n");
    } else {
      g_print("E");
      *status_line_ptr++ = 'E';
      if ( status_line_ptr >= status_line_end ) {
        g_print("\n");
        reset_status_line();
        test_header();
      }
    }
  }

  current_test = NULL;

  if ( verbose )
    reset_status_line();

  /* clear errno */
  if ( errno != 0 ) {
    g_log( G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
           "clearing errno left by test `%s' (%d: %s)",
           title, errno, g_strerror(errno) );
    errno = 0;
  }

  if ( teardown_func != NULL )
    teardown_func();

  /* clear errno */
  if ( errno != 0 ) {
    g_log( G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,
           "clearing errno after test cleanup for `%s' (%d: %s)",
           title, errno, g_strerror(errno) );
    errno = 0;
  }
}

/****************************************************************************/
/****************************************************************************/

void
test_suite_set_setup(TestSetupFunction func)
{
  setup_func = func;
}

void
test_suite_set_teardown(TestTearDownFunction func)
{
  teardown_func = func;
}

void
test_run_suite( SuiteFunction suite, gchar *title )
{
  /* keep track previous failures */
  int prev_failures = test_assertion_failed;

  reset_status_line();

  current_suite = title;
  suite_header();
  if ( verbose )
    g_print( "\n" );

  /* run the suite */
  suite();

  /* clear suite vars */
  current_suite = NULL;
  setup_func    = NULL;
  teardown_func = NULL;

  if ( !verbose ) {
    /* see if there were any new failures */
    if ( test_assertion_failed > prev_failures )
      g_print(" FAIL\r[" COLOR(1) COLOR_BOLD "FAIL" COLOR_RESET "]\n");
    else
      g_print(" PASS\r[" COLOR(2) COLOR_BOLD "PASS" COLOR_RESET "]\n");
  }
}

/****************************************************************************/
/****************************************************************************/

/** Test setup function for tests that need a temp file */
void
test_temp_file_setup()
{
  GError *error = NULL;

  /* make a temp file */
  test_temp_fd = g_file_open_tmp( NULL, &test_temp_name, &error );
  if ( test_temp_fd < 0 ) {
    g_log( G_LOG_DOMAIN, G_LOG_LEVEL_ERROR,
           "can't create test file: %s",
           error->message );
  }

  /* make sure it works */
  if ( lseek(test_temp_fd, 0, SEEK_SET) != 0 ) {
    g_log( G_LOG_DOMAIN, G_LOG_LEVEL_ERROR,
           "can't seek in test file: %s",
           g_strerror(errno) );
  }

}

/** Test teardown function for tests that need a temp file */
void
test_temp_file_teardown()
{
  g_assert( test_temp_fd   >= 0    );
  g_assert( test_temp_name != NULL );

  close( test_temp_fd );
  test_temp_fd = -1;

  g_unlink( test_temp_name );
  g_free( test_temp_name );
  test_temp_name = NULL;
}


int
main(int UNUSED(argc), char UNUSED(**argv))
{
	return test_main();
/*
#if !UNITTEST
  struct event sigint_ev;
#endif

  int rc = -1;

  event_set_log_callback( event_log );
  signal( SIGSEGV, crash_cb );
#ifndef WIN32
  signal( SIGBUS,  crash_cb );
#endif
  signal( SIGFPE,  crash_cb );
  g_log_set_handler(NULL, G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_ERROR | G_LOG_FLAG_FATAL, die_handler, NULL);
  parse_command_line(argc, argv);

#if UNITTEST
  signal( SIGINT, sigint_cb );
  event_init();
  rc = test_main();
#else
  event_init();
  signal_set(&sigint_ev, SIGINT, sigint_cb, NULL);
  event_add(&sigint_ev, NULL);
  http_start_server( http_listen_address, (gushort)http_port );
  event_dispatch();
  rc = 0;
#endif

  event_base_free(NULL);

  if (profile_memory)
    g_mem_profile();

  return rc;
*/

}
