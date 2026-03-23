#pragma once

#include "Str.h"
#include "Result.h"

struct TransactionRecord;

namespace SMS_Parser
{
	hstl::Result<hstl::Str_View> extract_raw_json_string(hstl::Str_View json_body, const char* key);

	// Analyzes a raw bank SMS using the Gemini AI API and maps the 
	// extracted data directly into the provided TransactionRecord.
	// Returns true if the extraction was successful and the amount is valid.
	// This method is designed to handle a wide variety of SMS formats and languages, leveraging AI to understand the content rather than relying on rigid parsing rules.
	// Though it involves a network call and is slower than traditional parsing, it can significantly increase the accuracy and flexibility of SMS processing, especially for non-standard or multilingual messages.
	// This is just a quick easy way to get better parsing results without having to write complex regexes or string manipulation code, just to produce an MVP.
	hstl::Result<bool> parse_sms_via_ai(hstl::Str_View raw_sms, TransactionRecord* record);
}