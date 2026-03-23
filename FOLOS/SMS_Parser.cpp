#include "SMS_Parser.h"
#include "Str_Format.h"
#include "Log.h"
#include "Database.h"
#include "Result.h"

// Required to make HTTPS requests to the Google API
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <string> // Used strictly for std::stol

namespace SMS_Parser
{
	static constexpr uint32_t PARSED_SMS_PARTS = 4u;

	hstl::Result<hstl::Str_View> extract_raw_json_string(hstl::Str_View json_body, const char* key)
	{
		hstl::Fixed_Str<32> search_key;
		search_key.push('"');
		search_key.push(key);
		search_key.push_range("\": \"", 4);

		size_t start = json_body.find(search_key.c_str());
		if (start == hstl::Str_View::npos)
			return hstl::Err("Key '{}' not found in JSON", key);

		start += search_key.count();

		hstl::Str_View remaining = json_body.substr(start, json_body.count() - start);
		size_t relative_end = remaining.find('"');
		if (relative_end == hstl::Str_View::npos)
			return hstl::Err("Malformed JSON: missing closing quote for key '{}'", key);

		return json_body.substr(start, relative_end);
	}

	hstl::Result<bool> parse_sms_via_ai(hstl::Str_View raw_sms, TransactionRecord* record)
	{
		httplib::Client cli("https://generativelanguage.googleapis.com");
		cli.set_connection_timeout(5);
		cli.set_read_timeout(5);

		// NOTE: Relax, this is a free tier of the Gemini API. Though For production use, we should secure your API key properly.
		const char* path = "/v1beta/models/gemini-2.5-flash:generateContent?key=AIzaSyBmGgwQ3k36CQlaHG5ChqIiMToTXpAhPzQ";

		// Sanitize the SMS
		// Replaces characters that would break the manually formatted JSON payload
		hstl::Str safe_sms;
		for (size_t i = 0; i < raw_sms.count(); ++i)
		{
			char c = raw_sms[i];
			if (c == '"' || c == '\\' || c == '\n' || c == '\r')
			{
				safe_sms.push(' ');
			}
			else
			{
				safe_sms.push(c);
			}
		}

		hstl::Str payload = hstl::fmt_str(
			R"({"contents": [{"parts": [{"text": "Extract amount, currency, date (DD/MM), and merchant. If the currency is in arabic replace it with 'EGP'. Reply ONLY with values separated by pipes like this: AMOUNT|CURRENCY|DATE|MERCHANT. No spaces around pipes. SMS: {}"}]}], "generationConfig": { "temperature": 0.0 }})",
			safe_sms.view()
		);

		auto res = cli.Post(path, payload.c_str(), "application/json");
		if (res && res->status == 200)
		{
			hstl::Str_View body(res->body.c_str(), res->body.length());
			auto text_res = extract_raw_json_string(body, "text");
			if (!text_res)
				return hstl::Err("Failed to extract 'text' from AI response: {}", text_res.get_err().get_message());
			auto parts = text_res.get_value().split('|');

			if (parts.count() == PARSED_SMS_PARTS)
			{
				record->amount = std::stol(std::string(parts[0].data(), parts[0].count()));

				// Parse Currency
				record->currency.clear();
				record->currency.push_range(parts[1].data(), parts[1].count());

				// Parse Date
				record->date.clear();
				record->date.push_range(parts[2].data(), parts[2].count());

				// Parse Merchant
				record->merchant.clear();
				record->merchant.push_range(parts[3].data(), parts[3].count());

				if (record->amount > 0)
					return true;

				return hstl::Err("Parsed amount is 0 or negative.");
			}

			return hstl::Err("Failed to parse AI response into exactly {} parts. Received {} parts.", PARSED_SMS_PARTS, parts.count());
		}

		if (res)
			return hstl::Err("AI API Call Failed. HTTP Status: {}. Response: {}", res->status, res->body.c_str());\

		return hstl::Err("AI API Call Failed (Network error or timeout)");
	}
}