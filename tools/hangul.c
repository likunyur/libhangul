#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <locale.h>
#include <getopt.h>
#include <errno.h>

#include <iconv.h>

#include "../hangul/hangul.h"

#define _(x) (x)

#ifdef WORDS_BIGENDIAN
#define UCS4  "UCS-4BE"
#else
#define UCS4  "UCS-4LE"
#endif

static const char* program_name = "hangul";
static iconv_t cd_ucs4_to_utf8 = (iconv_t)-1;

static void
print_error(int status, int errnum, const char* format, ...)
{
    va_list ap;

    printf("%s: %s: ", program_name, strerror(errnum));

    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);

    fputc('\n', stdout);
}

static void
usage(int status)
{
    if (status == EXIT_SUCCESS) {
	printf(_("\
Usage: %s [OPTION]... [FILE]...\n\
"), program_name);

	fputs(_("\
Convert string into korean characters according to korean keyboard layout.\n\
\n\
Mandatory arguments to long options are mandatory for short options too.\n\
  -k, --keyboard=KEYBOARDID   select keyboard layout by KEYBOARDID\n\
  -l, --list                  list available keyboard and exit\n\
  -i, --input=STRING          use STRING as input instead of standard input\n\
  -o, --output=FILE           write result to FILE instead of standard output\n\
  -s, --strict-order          do not allow wrong input sequence\n\
"), stdout);

	fputs(_("\
      --help                  display this help and exit\n\
      --version               output version information and exit\n\
"), stdout);

	fputs(_("\
\n\
With no FILE, or when FILE is -, read standard input.\n\
"), stdout);

         printf(_("\
\n\
Examples:\n\
  %s -i gksrmfdlqfur  Convert specified string into korean characters.\n\
  %s                  Convert standard input into korean characters\n\
                             and print them to standard output.\n\
"),
              program_name, program_name);
    } else {
	fprintf(stderr, _("Try `%s --help' for more information.\n"),
		 program_name);
    }

    exit(status);
}

static void
version()
{
    printf("%s (%s) %s\n", program_name, PACKAGE_NAME, PACKAGE_VERSION);
    exit(EXIT_SUCCESS);
}

static void
list_keyboards()
{
    unsigned i;
    unsigned n;

    n = hangul_ic_get_n_keyboards();
    for (i = 0; i < n; ++i) {
	const char* id;
	const char* name;

	id = hangul_ic_get_keyboard_id(i);
	name = hangul_ic_get_keyboard_name(i);

	printf("%-12s %s\n", id, name);
    }

    exit(EXIT_SUCCESS);
}

static bool
on_hic_transition(HangulInputContext* ic,
	 ucschar c, const ucschar* preedit, void * data)
{
    if (hangul_is_choseong(c)) {
	if (hangul_ic_has_jungseong(ic) || hangul_ic_has_jongseong(ic))
	    return false;
    } else if (hangul_is_jungseong(c)) {
	if (hangul_ic_has_jongseong(ic))
	    return false;
    }

    return true;
}

size_t ucschar_strlen(const ucschar* str)
{
    const ucschar* p = str;
    while (p[0] != 0) {
	p++;
    }

    return p - str;
}

static int
fputs_ucschar(const ucschar* str, FILE* stream)
{
    char buf[512];
    char* inbuf;
    char* outbuf;
    size_t inbytesleft;
    size_t outbytesleft;
    size_t len;
    size_t res;

    len = ucschar_strlen(str);

    inbuf = (char*)str;
    outbuf = buf;
    inbytesleft = len * 4;
    outbytesleft = sizeof(buf) - 1;

    res = iconv(cd_ucs4_to_utf8, &inbuf, &inbytesleft, &outbuf, &outbytesleft);
    if (res == -1) {
	if (errno == E2BIG) {
	} else if (errno == EILSEQ) {
	} else if (errno == EINVAL) {
	}
    }
    
    *outbuf = '\0';

    return fputs(buf, stream);
}

static void
hangul_process_with_string(HangulInputContext* ic, const char* input, FILE* output)
{
    int r;
    const ucschar* str;

    while (*input != '\0') {
	bool is_processed = hangul_ic_process(ic, *input);
	str = hangul_ic_get_commit_string(ic);
	if (str[0] != 0) {
	    r = fputs_ucschar(str, output);
	    if (r == EOF)
		goto on_error;
	}
	if (!is_processed) {
	    r = fputc(*input, output);
	    if (r == EOF)
		goto on_error;
	}

	input++;
    }

    str = hangul_ic_flush(ic);
    if (str[0] != 0) {
	r = fputs_ucschar(str, output);
	if (r == EOF)
	    goto on_error;
    }

    r = fputs("\n", output);
    if (r == EOF)
	goto on_error;

    return;

on_error:
    print_error(0, errno, _("standard output"));
    exit(EXIT_FAILURE);
}

static void
hangul_process(HangulInputContext* ic, FILE* input, FILE* output)
{
    int r;
    int c;
    const ucschar* str;

    c = fgetc(input);
    while (c != EOF) {
	bool res = hangul_ic_process(ic, c);
	str = hangul_ic_get_commit_string(ic);
	if (str[0] != 0) {
	    r = fputs_ucschar(str, output);
	    if (r == EOF)
		goto on_error;
	}
	if (!res) {
	    r = fputc(c, output);
	    if (r == EOF)
		goto on_error;
	}

	c = fgetc(input);
    }

    str = hangul_ic_flush(ic);
    if (str[0] != 0) {
	r = fputs_ucschar(str, output);
	if (r == EOF)
	    goto on_error;
    }

    return;

on_error:
    print_error(0, errno, _("standard output"));
    exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
    int i;
    int res;
    const char* input_string;
    const char* output_file;
    const char* keyboard;
    FILE* output;
    HangulInputContext* ic;
    bool strict_order = false;

    setlocale(LC_ALL, "");

    res = EXIT_SUCCESS;
    keyboard = "2";
    input_string = NULL;
    output_file = "-";
    while (1) {
	int c;
	static struct option const long_options[] = {
	    { "keyboard",    required_argument,  NULL, 'k' },
	    { "list",        no_argument,        NULL, 'l' },
	    { "input",       required_argument,  NULL, 'i' },
	    { "output",      required_argument,  NULL, 'o' },
	    { "strict-order",no_argument,        NULL, 's' },
	    { "help",        no_argument,        NULL, 'h' },
	    { "version",     no_argument,        NULL, 'v' },
	    { NULL,          0,                  NULL, 0   }
	};

	c = getopt_long(argc, argv, "k:li:o:s", long_options, NULL);
	if (c == -1)
	    break;

	switch (c) {
	case 'k':
	    keyboard = optarg;
	    break;
	case 'l':
	    list_keyboards();
	    break;
	case 'i':
	    input_string = optarg;
	    break;
	case 'o':
	    output_file = optarg;
	    break;
	case 's':
	    strict_order = true;
	    break;
	case 'h':
	    usage(EXIT_SUCCESS);
	    break;
	case 'v':
	    version();
	    break;
	default:
	    usage(EXIT_FAILURE);
	}
    }

    if (strcmp(output_file, "-") == 0) {
	output = stdout;
    } else {
	output = fopen(output_file, "w");
	if (output == NULL) {
	    print_error(EXIT_FAILURE, errno, "%s", output_file);
	    exit(EXIT_FAILURE);
	}
    }

    cd_ucs4_to_utf8 = iconv_open("UTF-8", UCS4);
    if (cd_ucs4_to_utf8 == (iconv_t)-1) {
	print_error(EXIT_FAILURE, errno, _("conversion from %s to UTF-8"), UCS4);
	exit(EXIT_FAILURE);
    }

    ic = hangul_ic_new(keyboard);

    if (strict_order) {
	hangul_ic_connect_callback(ic, "transition", on_hic_transition, NULL);
    }

    if (input_string != NULL) {
	hangul_process_with_string(ic, input_string, output);
    }

    if (optind < argc) {
	for (i = optind; i < argc; i++) {
	    FILE* input = NULL;
	    if (strcmp(argv[i], "-") == 0) {
		input = stdin;
		hangul_process(ic, input, output);
	    } else {
		input = fopen(argv[i], "r");
		if (input == NULL) {
		    print_error(0, errno, "%s", argv[i]);
		} else {
		    hangul_process(ic, input, output);
		    fclose(input);
		}
	    }
	}
    } else if (input_string == NULL) {
	hangul_process(ic, stdin, output);
    }

    hangul_ic_delete(ic);

    iconv_close(cd_ucs4_to_utf8);

    if (strcmp(output_file, "-") != 0) {
	fclose(output);
    }

    return res;
}