#include "unicode/ucsdet.h"
#include "magic.h"
#include "common.h"

extern VALUE rb_mCharlockHolmes;
static VALUE rb_cEncodingDetector;

typedef struct {
	UCharsetDetector *csd;
	magic_t magic;
} charlock_detector_t;

static VALUE rb_encdec_buildmatch(const UCharsetMatch *match)
{
	UErrorCode status = U_ZERO_ERROR;
	const char *mname;
	const char *mlang;
	int mconfidence;
	VALUE rb_match;

	if (!match)
		return Qnil;

	mname = ucsdet_getName(match, &status);
	mlang = ucsdet_getLanguage(match, &status);
	mconfidence = ucsdet_getConfidence(match, &status);

	rb_match = rb_hash_new();

	rb_hash_aset(rb_match, ID2SYM(rb_intern("type")), ID2SYM(rb_intern("text")));
	rb_hash_aset(rb_match, ID2SYM(rb_intern("encoding")), charlock_new_str2(mname));
	rb_hash_aset(rb_match, ID2SYM(rb_intern("confidence")), INT2NUM(mconfidence));

	if (mlang && mlang[0])
		rb_hash_aset(rb_match, ID2SYM(rb_intern("language")), charlock_new_str2(mlang));

	return rb_match;
}

static VALUE rb_encdec_binarymatch() {
	VALUE rb_match;

	rb_match = rb_hash_new();

	rb_hash_aset(rb_match, ID2SYM(rb_intern("type")), ID2SYM(rb_intern("binary")));
	rb_hash_aset(rb_match, ID2SYM(rb_intern("confidence")), INT2NUM(100));

	return rb_match;
}

static int detect_binary_content(charlock_detector_t *detector, VALUE rb_str) {
	const char *binary_result;

	binary_result = magic_buffer(detector->magic, RSTRING_PTR(rb_str), RSTRING_LEN(rb_str));

	if (binary_result) {
		if (strstr(binary_result, "library") ||
				strstr(binary_result, "bundle") ||
				strstr(binary_result, "archive") ||
				(!strstr(binary_result, "text") && strstr(binary_result, "executable")) ||
				strstr(binary_result, "data"))
			return 1;
	} else {
		rb_raise(rb_eStandardError, magic_error(detector->magic));
	}

	return 0;
}

/*
 * call-seq: detection_hash = EncodingDetector.detect str[, hint_enc]
 *
 * Attempt to detect the encoding of this string
 *
 * str      - a String, what you want to detect the encoding of
 * hint_enc - an optional String (like "UTF-8"), the encoding name which will
 *            be used as an additional hint to the charset detector
 *
 * Returns: a Hash with :encoding, :language, :type and :confidence
 */
static VALUE rb_encdec_detect(int argc, VALUE *argv, VALUE self)
{
	UErrorCode status = U_ZERO_ERROR;
	charlock_detector_t *detector;
	VALUE rb_str;
	VALUE rb_enc_hint;

	rb_scan_args(argc, argv, "11", &rb_str, &rb_enc_hint);

	Check_Type(rb_str, T_STRING);
	Data_Get_Struct(self, charlock_detector_t, detector);

	// first lets see if this is binary content
	if (detect_binary_content(detector, rb_str)) {
		return rb_encdec_binarymatch();
	}

	// if we got here - the data doesn't look like binary
	// lets try to figure out what encoding the text is in
	ucsdet_setText(detector->csd, RSTRING_PTR(rb_str), (int32_t)RSTRING_LEN(rb_str), &status);

	if (!NIL_P(rb_enc_hint)) {
		Check_Type(rb_enc_hint, T_STRING);
		ucsdet_setDeclaredEncoding(detector->csd, RSTRING_PTR(rb_enc_hint), RSTRING_LEN(rb_enc_hint), &status);
	}

	return rb_encdec_buildmatch(ucsdet_detect(detector->csd, &status));
}


/*
 * call-seq: detection_hash_array = EncodingDetector.detect_all str[, hint_enc]
 *
 * Attempt to detect the encoding of this string, and return
 * a list with all the possible encodings that match it.
 *
 *
 * str      - a String, what you want to detect the encoding of
 * hint_enc - an optional String (like "UTF-8"), the encoding name which will
 *            be used as an additional hint to the charset detector
 *
 * Returns: an Array with zero or more Hashes,
 *          each one of them with with :encoding, :language, :type and :confidence
 */
static VALUE rb_encdec_detect_all(int argc, VALUE *argv, VALUE self)
{
	UErrorCode status = U_ZERO_ERROR;
	charlock_detector_t *detector;
	const UCharsetMatch **csm;
	VALUE rb_ret;
	int i, match_count;
	VALUE rb_str;
	VALUE rb_enc_hint;
	VALUE binary_match;

	rb_scan_args(argc, argv, "11", &rb_str, &rb_enc_hint);

	Check_Type(rb_str, T_STRING);
	Data_Get_Struct(self, charlock_detector_t, detector);

	rb_ret = rb_ary_new();

	// first lets see if this is binary content
	binary_match = Qnil;
	if (detect_binary_content(detector, rb_str)) {
		binary_match = rb_encdec_binarymatch();
	}

	ucsdet_setText(detector->csd, RSTRING_PTR(rb_str), (int32_t)RSTRING_LEN(rb_str), &status);

	if (!NIL_P(rb_enc_hint)) {
		Check_Type(rb_enc_hint, T_STRING);
		ucsdet_setDeclaredEncoding(detector->csd, RSTRING_PTR(rb_enc_hint), RSTRING_LEN(rb_enc_hint), &status);
	}

	csm = ucsdet_detectAll(detector->csd, &match_count, &status);

	for (i = 0; i < match_count; ++i) {
		rb_ary_push(rb_ret, rb_encdec_buildmatch(csm[i]));
	}

	if (!NIL_P(binary_match))
		rb_ary_unshift(rb_ret, binary_match);

	return rb_ret;
}

/*
 * call-seq: EncodingDetector#strip_tags?
 *
 * Returns whether or not the strip_tags flag is set on this detector
 *
 * Returns: Boolean
 */
static VALUE rb_get_strip_tags(VALUE self)
{
	charlock_detector_t *detector;
	UBool val;
	VALUE rb_val;

	Data_Get_Struct(self, charlock_detector_t, detector);

	val = ucsdet_isInputFilterEnabled(detector->csd);

	rb_val = val == 1 ? Qtrue : Qfalse;

	return rb_val;
}

/*
 * call-seq: EncodingDetector#strip_tags = true
 *
 * Enable or disable the stripping of HTML/XML tags from the input before
 * attempting any detection
 *
 * Returns: Boolean, the value passed
 */
static VALUE rb_set_strip_tags(VALUE self, VALUE rb_val)
{
	charlock_detector_t *detector;
	UBool val;

	Data_Get_Struct(self, charlock_detector_t, detector);

	val = rb_val == Qtrue ? 1 : 0;

	ucsdet_enableInputFilter(detector->csd, val);

	return rb_val;
}

/*
 * call-seq: detectable_encodings = EncodingDetector.supported_encodings
 *
 * The list of detectable encodings supported by this library
 *
 * Returns: an Array of Strings
 */
static VALUE rb_get_supported_encodings(VALUE klass)
{
	UCharsetDetector *csd;
	UErrorCode status = U_ZERO_ERROR;
	UEnumeration *encoding_list;
	VALUE rb_encoding_list;
	int32_t enc_count;
	int32_t i;
	const char *enc_name;
	int32_t enc_name_len;

	rb_encoding_list = rb_iv_get(klass, "encoding_list");

	// lazily populate the list
	if (NIL_P(rb_encoding_list)) {
		csd = ucsdet_open(&status);

		encoding_list = ucsdet_getAllDetectableCharsets(csd, &status);
		rb_encoding_list = rb_ary_new();
		enc_count = uenum_count(encoding_list, &status);

		for(i=0; i < enc_count; i++) {
			enc_name = uenum_next(encoding_list, &enc_name_len, &status);
			rb_ary_push(rb_encoding_list, charlock_new_str(enc_name, enc_name_len));
		}

		rb_iv_set(klass, "encoding_list", rb_encoding_list);
		ucsdet_close(csd);
	}

	return rb_encoding_list;
}

static void rb_encdec__free(void *obj)
{
	charlock_detector_t *detector;

	detector = (charlock_detector_t *)obj;

	if (detector->csd)
		ucsdet_close(detector->csd);

	if (detector->magic)
		magic_close(detector->magic);

	free(detector);
}

static VALUE rb_encdec__alloc(VALUE klass)
{
	charlock_detector_t *detector;
	UErrorCode status = U_ZERO_ERROR;
	VALUE obj;

	detector = malloc(sizeof(charlock_detector_t*));
	obj = Data_Wrap_Struct(klass, NULL, rb_encdec__free, (void *)detector);

	detector->csd = ucsdet_open(&status);
	if (U_FAILURE(status)) {
		rb_raise(rb_eStandardError, u_errorName(status));
	}

	detector->magic = magic_open(0);
	if (detector->magic == NULL) {
		rb_raise(rb_eStandardError, magic_error(detector->magic));
	}

	// load the libmagic database
	// NULL means use the default or whatever is specified by the MAGIC env var
	magic_load(detector->magic, NULL);

	return obj;
}

void _init_charlock_encoding_detector()
{
	rb_cEncodingDetector = rb_define_class_under(rb_mCharlockHolmes, "EncodingDetector", rb_cObject);
	rb_define_alloc_func(rb_cEncodingDetector, rb_encdec__alloc);
	rb_define_method(rb_cEncodingDetector, "detect", rb_encdec_detect, -1);
	rb_define_method(rb_cEncodingDetector, "detect_all", rb_encdec_detect_all, -1);
	rb_define_method(rb_cEncodingDetector, "strip_tags", rb_get_strip_tags, 0);
	rb_define_method(rb_cEncodingDetector, "strip_tags=", rb_set_strip_tags, 1);

	rb_define_singleton_method(rb_cEncodingDetector, "supported_encodings", rb_get_supported_encodings, 0);
}