#include "httplib.h"
#include "Log.h"
#include "Str.h"
#include "Result.h"
#include "Str_Format.h"
#include "Fixed_Str.h"
#include "sqlite3.h"

#include <cstdio>
#include <string>

using namespace hstl;

const char* DASHBOARD_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Transaction Dashboard</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #f4f4f9; padding: 20px; color: #333; }
        .header-container { max-width: 1200px; margin: 0 auto; display: flex; justify-content: space-between; align-items: center; }
        h1 { color: #2c3e50; }
        table { width: 100%; max-width: 1200px; margin: 20px auto; border-collapse: collapse; background: white; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        th, td { padding: 12px 15px; text-align: left; border-bottom: 1px solid #ddd; }
        th { background-color: #2c3e50; color: white; text-transform: uppercase; font-size: 14px; }
        tr:hover { background-color: #f9f9f9; }
        
        .month-header { background-color: #ecf0f1 !important; border-top: 2px solid #bdc3c7; }
        .month-title { font-weight: bold; color: #2c3e50; font-size: 16px; }
        .month-total { font-weight: bold; color: #e67e22; font-size: 15px; text-align: right; }
        
        .amount { font-weight: bold; color: #27ae60; }
        .note-text { font-size: 13px; color: #555; font-style: italic; }
        button { cursor: pointer; border: none; border-radius: 4px; padding: 6px 12px; font-weight: bold; transition: background 0.2s; }
        
        .btn-edit { background: #3498db; color: white; font-size: 10px; padding: 4px 8px; margin-left: 8px; }
        .btn-edit:hover { background: #2980b9; }
        .btn-delete { background: #e74c3c; color: white; font-size: 12px; }
        .btn-delete:hover { background: #c0392b; }
        .btn-clear { background: #c0392b; color: white; padding: 10px 15px; }
        .btn-clear:hover { background: #962d22; }
    </style>
</head>
<body>
    <div class="header-container">
        <h1>Transaction Dashboard</h1>
        <button class="btn-clear" onclick="clearAllTransactions()">Clear All</button>
    </div>
    
    <table>
        <thead>
            <tr>
                <th>ID</th>
                <th>Date</th>
                <th>Merchant</th>
                <th>Amount</th>
                <th>Notes</th>
                <th>System Log Time</th>
                <th>Actions</th>
            </tr>
        </thead>
        <tbody id="tx-table-body">
            <tr><td colspan="7" style="text-align: center;">Loading...</td></tr>
        </tbody>
    </table>

    <script>
        async function loadTransactions() {
            try {
                const response = await fetch('/api/transactions');
                const data = await response.json();
                const tbody = document.getElementById('tx-table-body');
                tbody.innerHTML = '';

                if (data.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="7" style="text-align: center;">No transactions found.</td></tr>';
                    return;
                }

                const grouped = {};
                data.forEach(tx => {
                    const month = (tx.date && tx.date !== "Unknown") ? tx.date.substring(0, 7) : "Unknown"; 
                    
                    if (!grouped[month]) {
                        grouped[month] = { transactions: [], totals: {} };
                    }
                    grouped[month].transactions.push(tx);
                    
                    if (!grouped[month].totals[tx.currency]) {
                        grouped[month].totals[tx.currency] = 0;
                    }
                    grouped[month].totals[tx.currency] += Number(tx.amount);
                });

                const sortedMonths = Object.keys(grouped).sort((a, b) => {
                    if (a === "Unknown") return 1;
                    if (b === "Unknown") return -1;
                    return b.localeCompare(a);
                });

                sortedMonths.forEach(month => {
                    const group = grouped[month];
                    
                    let monthName = "⚠️ Unknown Date";
                    if (month !== "Unknown") {
                        const dateObj = new Date(month + "-01");
                        monthName = "📅 " + dateObj.toLocaleString('default', { month: 'long', year: 'numeric' });
                    }

                    const totalStr = Object.entries(group.totals)
                        .map(([currency, amount]) => `${amount.toFixed(2)} ${currency}`)
                        .join(' + ');

                    const headerRow = document.createElement('tr');
                    headerRow.className = 'month-header';
                    headerRow.innerHTML = `
                        <td colspan="3" class="month-title">${monthName}</td>
                        <td colspan="4" class="month-total">Total: ${totalStr}</td>
                    `;
                    tbody.appendChild(headerRow);

                    group.transactions.forEach(tx => {
                        const safeNote = tx.notes ? tx.notes.replace(/'/g, "\\'").replace(/"/g, "&quot;") : '';
                        const displayNote = tx.notes || '-';
                        
                        const row = document.createElement('tr');
                        row.innerHTML = `
                            <td>#${tx.id}</td>
                            <td>${tx.date}</td>
                            <td>${tx.merchant}</td>
                            <td class="amount">${Number(tx.amount).toFixed(2)} ${tx.currency}</td>
                            <td>
                                <span class="note-text">${displayNote}</span>
                                <button class="btn-edit" onclick="editNote(${tx.id}, '${safeNote}')">Edit</button>
                            </td>
                            <td style="color:#7f8c8d; font-size:12px;">${tx.created_at}</td>
                            <td>
                                <button class="btn-delete" onclick="deleteTransaction(${tx.id})">Delete</button>
                            </td>
                        `;
                        tbody.appendChild(row);
                    });
                });
            } catch (err) {
                console.error('Failed to fetch transactions', err);
                const tbody = document.getElementById('tx-table-body');
                tbody.innerHTML = '<tr><td colspan="7" style="text-align: center; color: red;">Error loading transactions.</td></tr>';
            }
        }

        async function editNote(id, currentNote) {
            const newNote = prompt(`Enter note for transaction #${id}:`, currentNote);
            if (newNote === null) return; 
            
            try {
                const response = await fetch(`/api/transactions/${id}/note`, { 
                    method: 'PUT',
                    body: newNote 
                });
                if (response.ok) {
                    loadTransactions(); 
                } else {
                    alert('Failed to update note.');
                }
            } catch (err) {
                console.error(err);
                alert('An error occurred while saving the note.');
            }
        }

        async function deleteTransaction(id) {
            if (!confirm(`Are you sure you want to delete transaction #${id}?`)) return;
            try {
                const response = await fetch(`/api/transactions/${id}`, { method: 'DELETE' });
                if (response.ok) loadTransactions(); 
                else alert('Failed to delete transaction.');
            } catch (err) {
                alert('An error occurred while deleting.');
            }
        }

        async function clearAllTransactions() {
            if (!confirm('WARNING: Are you sure you want to delete ALL transactions?')) return;
            try {
                const response = await fetch('/api/transactions', { method: 'DELETE' });
                if (response.ok) loadTransactions(); 
                else alert('Failed to clear database.');
            } catch (err) {
                alert('An error occurred while clearing.');
            }
        }
        
        loadTransactions();
    </script>
</body>
</html>
)HTML";

struct TransactionRecord {
	long amount{ 0 };
	hstl::Str currency;
	hstl::Str merchant;
	hstl::Str date;
	hstl::Str notes;
	hstl::Str raw_sms;
};

hstl::Result<sqlite3*> init_database(const char* db_name) {
	sqlite3* db = nullptr;
	if (sqlite3_open(db_name, &db) != SQLITE_OK) {
		return hstl::Err("Failed to open database: {}", sqlite3_errmsg(db));
	}

	const char* create_table_sql =
		"CREATE TABLE IF NOT EXISTS transactions ("
		"id INTEGER PRIMARY KEY AUTOINCREMENT,"
		"amount INTEGER,"
		"currency TEXT,"
		"merchant TEXT,"
		"date TEXT,"
		"notes TEXT DEFAULT '',"
		"raw_sms TEXT DEFAULT '',"
		"created_at DATETIME DEFAULT CURRENT_TIMESTAMP);";

	char* err_msg = nullptr;
	if (sqlite3_exec(db, create_table_sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
		hstl::Err err("Failed to create table: {}", (const char*)err_msg);
		sqlite3_free(err_msg);
		sqlite3_close(db);
		return err;
	}

	return db;
}

hstl::Result<bool> save_transaction(sqlite3* db, const TransactionRecord& record) {
	const char* insert_sql = "INSERT INTO transactions (amount, currency, merchant, date, notes, raw_sms) VALUES (?, ?, ?, ?, ?, ?);";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return hstl::Err("Failed to prepare statement: {}", sqlite3_errmsg(db));
	}

	sqlite3_bind_int64(stmt, 1, record.amount);

	hstl::Str_View currency_v = record.currency.view();
	sqlite3_bind_text(stmt, 2, currency_v.data(), (int)currency_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View merchant_v = record.merchant.view();
	sqlite3_bind_text(stmt, 3, merchant_v.data(), (int)merchant_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View date_v = record.date.view();
	sqlite3_bind_text(stmt, 4, date_v.data(), (int)date_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View notes_v = record.notes.view();
	sqlite3_bind_text(stmt, 5, notes_v.data(), (int)notes_v.count(), SQLITE_TRANSIENT);

	hstl::Str_View raw_sms_v = record.raw_sms.view();
	sqlite3_bind_text(stmt, 6, raw_sms_v.data(), (int)raw_sms_v.count(), SQLITE_TRANSIENT);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		hstl::Err err("Failed to execute statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return err;
	}

	sqlite3_finalize(stmt);
	return true;
}

void normalize_date(hstl::Str& date_str) {
	if (date_str.view().count() != 5 || date_str.view()[2] != '/') {
		return;
	}

	hstl::Str_View view = date_str.view();

	hstl::Fixed_Str<3> day;
	day.push_range(view.data(), 2);

	hstl::Fixed_Str<3> month;
	month.push_range(view.data() + 3, 2);

	std::time_t t = std::time(nullptr);
	std::tm* now = std::localtime(&t);
	int current_year = now->tm_year + 1900;
	int current_month = now->tm_mon + 1;

	int msg_month = std::atoi(month.c_str());

	if (msg_month == 12 && current_month == 1) {
		current_year--;
	}

	auto formatted = hstl::fmt_fixed_str<16>("{}-{}-{}", current_year, month.view(), day.view());

	date_str.clear();
	date_str.push_range(formatted.c_str(), formatted.count());
}

hstl::Str_View extract_json_value(hstl::Str_View payload, const char* key)
{
	char search_key[32];
	std::snprintf(search_key, sizeof(search_key), "\"%s\"", key);

	size_t key_pos = payload.find(search_key);
	if (key_pos == hstl::Str_View::npos) return hstl::Str_View("");

	hstl::Str_View after_key = payload.substr(key_pos, payload.count() - key_pos);
	size_t colon_pos = after_key.find(':');
	if (colon_pos == hstl::Str_View::npos) return hstl::Str_View("");

	hstl::Str_View after_colon = after_key.substr(colon_pos, after_key.count() - colon_pos);
	size_t start_quote = after_colon.find('"');
	if (start_quote == hstl::Str_View::npos) return hstl::Str_View("");

	hstl::Str_View value_area = after_colon.substr(start_quote + 1, after_colon.count() - (start_quote + 1));
	size_t end_quote = value_area.find('"');
	if (end_quote == hstl::Str_View::npos) return hstl::Str_View("");

	return value_area.substr(0, end_quote);
}

// Simple helper to prevent dashboard JSON from breaking if quotes/newlines are in the DB text
hstl::Str escape_json_string(const char* input) {
	hstl::Str out;
	if (!input) return out;

	size_t len = std::strlen(input);
	for (size_t i = 0; i < len; ++i) {
		if (input[i] == '"') out.push_range("\\\"", 2);
		else if (input[i] == '\\') out.push_range("\\\\", 2);
		else if (input[i] == '\n') out.push_range("\\n", 2);
		else if (input[i] == '\r') out.push_range("\\r", 2);
		else if (input[i] == '\t') out.push_range("\\t", 2);
		else out.push(input[i]);
	}
	return out;
}

hstl::Str get_transactions_json(sqlite3* db) {
	hstl::Str json;
	json.push('[');

	const char* query = "SELECT id, amount, currency, merchant, date, created_at, notes FROM transactions ORDER BY created_at DESC;";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) == SQLITE_OK) {
		bool first = true;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			if (!first) {
				json.push(',');
			}
			first = false;

			int id = sqlite3_column_int(stmt, 0);
			long amount = sqlite3_column_int64(stmt, 1);
			const char* currency = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
			const char* merchant = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
			const char* date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
			const char* created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
			const char* raw_notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

			hstl::Str escaped_notes = escape_json_string(raw_notes);

			hstl::fmt(json,
				R"({"id":{},"amount":{},"currency":"{}","merchant":"{}","date":"{}","created_at":"{}","notes":"{}"})",
				id, amount, currency, merchant, date, created_at, escaped_notes.view()
			);
		}
	}
	else {
		hstl::log_error("Failed to query transactions: {}", sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	json.push(']');
	return json;
}

hstl::Result<bool> delete_transaction(sqlite3* db, int id) {
	const char* delete_sql = "DELETE FROM transactions WHERE id = ?;";
	sqlite3_stmt* stmt;

	if (sqlite3_prepare_v2(db, delete_sql, -1, &stmt, nullptr) != SQLITE_OK) {
		return hstl::Err("Failed to prepare delete statement: {}", sqlite3_errmsg(db));
	}
	sqlite3_bind_int(stmt, 1, id);

	if (sqlite3_step(stmt) != SQLITE_DONE) {
		hstl::Err err("Failed to execute delete statement: {}", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return err;
	}

	sqlite3_finalize(stmt);
	return true;
}

hstl::Result<bool> clear_transactions(sqlite3* db) {
	char* err_msg = nullptr;
	if (sqlite3_exec(db, "DELETE FROM transactions;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
		hstl::Err err("Failed to clear transactions: {}", (const char*)err_msg);
		sqlite3_free(err_msg);
		return err;
	}
	sqlite3_exec(db, "DELETE FROM sqlite_sequence WHERE name='transactions';", nullptr, nullptr, nullptr);
	return true;
}

bool is_arabic_message(hstl::Str_View sms)
{
	if (sms.find("خصم") != hstl::Str_View::npos) return true;
	if (sms.find("حساب") != hstl::Str_View::npos) return true;
	if (sms.find("جم") != hstl::Str_View::npos) return true;
	if (sms.find("جنيه") != hstl::Str_View::npos) return true;
	return false;
}

bool parse_english_sms(hstl::Str_View sms, TransactionRecord& record)
{
	size_t debited_pos = sms.find("debited by");
	if (debited_pos == hstl::Str_View::npos) return false;

	size_t currency_pos = debited_pos + 11;
	hstl::Str_View currency_str = sms.substr(currency_pos, 3);
	record.currency.clear();
	record.currency.push_range(currency_str.data(), currency_str.count());

	size_t amount_start = currency_pos + 3;
	size_t amount_end = amount_start;

	while (amount_end < sms.count() && ((sms[amount_end] >= '0' && sms[amount_end] <= '9') || sms[amount_end] == '.' || sms[amount_end] == ','))
	{
		amount_end++;
	}

	hstl::Str_View amount_str = sms.substr(amount_start, amount_end - amount_start);

	std::string clean_amount;
	for (size_t i = 0; i < amount_str.count(); ++i) {
		if (amount_str[i] != ',') clean_amount += amount_str[i];
	}

	if (!clean_amount.empty()) {
		try {
			record.amount = std::stol(clean_amount);
		}
		catch (...) {
			return false;
		}
	}

	size_t slash_pos = hstl::Str_View::npos;
	for (size_t i = amount_end; i < sms.count(); ++i) {
		if (sms[i] == '/') {
			slash_pos = i;
			break;
		}
	}

	if (slash_pos != hstl::Str_View::npos && slash_pos >= 2 && slash_pos + 2 < sms.count()) {
		hstl::Str_View date_str = sms.substr(slash_pos - 2, 5);
		record.date.clear();
		record.date.push_range(date_str.data(), date_str.count());
	}
	else {
		record.date = hstl::Str("Unknown");
	}

	size_t hyphen_pos = sms.find("- ");
	if (hyphen_pos == hstl::Str_View::npos) return false;

	size_t merchant_start = hyphen_pos + 2;
	size_t merchant_end = sms.find(" ref");
	if (merchant_end == hstl::Str_View::npos) merchant_end = sms.find(". For");

	if (merchant_end == hstl::Str_View::npos || merchant_start >= merchant_end) return false;

	hstl::Str_View merchant_str = sms.substr(merchant_start, merchant_end - merchant_start);
	record.merchant.clear();
	record.merchant.push_range(merchant_str.data(), merchant_str.count());

	return true;
}

bool parse_arabic_sms(hstl::Str_View sms, TransactionRecord& record)
{
	size_t amount_start = hstl::Str_View::npos;
	for (size_t i = 0; i < sms.count(); ++i) {
		if (sms[i] >= '0' && sms[i] <= '9') {
			amount_start = i;
			break;
		}
	}

	if (amount_start == hstl::Str_View::npos) return false;

	size_t amount_end = amount_start;
	while (amount_end < sms.count() && ((sms[amount_end] >= '0' && sms[amount_end] <= '9') || sms[amount_end] == '.' || sms[amount_end] == ',')) {
		amount_end++;
	}

	hstl::Str_View amount_str = sms.substr(amount_start, amount_end - amount_start);

	std::string clean_amount;
	for (size_t i = 0; i < amount_str.count(); ++i) {
		if (amount_str[i] != ',') clean_amount += amount_str[i];
	}
	if (clean_amount.empty()) return false;

	try {
		record.amount = std::stol(clean_amount);
	}
	catch (...) {
		return false;
	}

	size_t currency_start = amount_end;
	while (currency_start < sms.count() && sms[currency_start] == ' ') currency_start++;

	size_t currency_end = currency_start;
	while (currency_end < sms.count() && sms[currency_end] != ' ') currency_end++;

	if (currency_start < currency_end) {
		hstl::Str_View currency_str = sms.substr(currency_start, currency_end - currency_start);

		// Normalize Arabic currency to "EGP" so the dashboard totals match up perfectly
		if (currency_str.find("جم") != hstl::Str_View::npos || currency_str.find("جنيه") != hstl::Str_View::npos) {
			record.currency.clear();
			record.currency.push_range("EGP", 3);
		}
		else {
			record.currency.clear();
			record.currency.push_range(currency_str.data(), currency_str.count());
		}
	}

	size_t slash_pos = hstl::Str_View::npos;
	for (size_t i = currency_end; i < sms.count(); ++i) {
		if (sms[i] == '/') {
			slash_pos = i;
			break;
		}
	}

	if (slash_pos != hstl::Str_View::npos && slash_pos >= 2 && slash_pos + 2 < sms.count()) {
		hstl::Str_View date_str = sms.substr(slash_pos - 2, 5);
		record.date.clear();
		record.date.push_range(date_str.data(), date_str.count());
	}
	else {
		record.date = hstl::Str("Unknown");
	}

	size_t merchant_start = hstl::Str_View::npos;
	if (slash_pos != hstl::Str_View::npos) {
		for (size_t i = slash_pos; i < sms.count(); ++i) {
			if (sms[i] == ' ') {
				merchant_start = i + 1;
				break;
			}
		}
	}
	else {
		const char* markers[] = { "فى ", "في ", "يوم ", "سحب ", "- " };
		for (const char* marker : markers) {
			size_t pos = sms.find(marker);
			if (pos != hstl::Str_View::npos && pos > currency_end) {
				merchant_start = pos + std::strlen(marker);
				break;
			}
		}
	}

	if (merchant_start == hstl::Str_View::npos) return false;

	size_t merchant_end = sms.find("الرصيد");
	if (merchant_end == hstl::Str_View::npos) merchant_end = sms.find("يمكنك");
	if (merchant_end == hstl::Str_View::npos) merchant_end = sms.count();

	while (merchant_start < merchant_end && (sms[merchant_start] == ' ' || sms[merchant_start] == '-')) {
		merchant_start++;
	}

	hstl::Str_View temp_merchant = sms.substr(merchant_start, merchant_end - merchant_start);
	const char* saheb = "سحب ";
	if (temp_merchant.find(saheb) == 0) {
		merchant_start += std::strlen(saheb);
	}

	while (merchant_end > merchant_start && sms[merchant_end - 1] == ' ') merchant_end--;

	if (merchant_start >= merchant_end) return false;

	hstl::Str_View merchant_str = sms.substr(merchant_start, merchant_end - merchant_start);
	record.merchant.clear();
	record.merchant.push_range(merchant_str.data(), merchant_str.count());

	return true;
}

int main() {
	auto db_res = init_database("transactions.db");
	if (!db_res) {
		hstl::log_error("Fatal: {}", db_res.get_err().get_message());
		return 1;
	}

	sqlite3* db = db_res.get_value();
	hstl::log_info("Database initialized successfully.");

	httplib::Server svr;

	svr.Post("/webhook", [db](const httplib::Request& req, httplib::Response& res) {
		if (!req.has_header("Authorization") || req.get_header_value("Authorization") != "Bearer MySecretToken123")
		{
			log_error("Rejected request: Invalid or Missing token.");
			res.status = 401;
			return;
		}

		hstl::Str_View payload(req.body.c_str(), req.body.length());
		hstl::Str_View raw_sms = extract_json_value(payload, "text");

		if (raw_sms.count() > 0)
		{
			TransactionRecord record;
			// Store the raw payload exactly as it arrived for debugging/safety
			record.raw_sms.push_range(raw_sms.data(), raw_sms.count());

			bool success = false;

			if (is_arabic_message(raw_sms)) {
				success = parse_arabic_sms(raw_sms, record);
				if (success) hstl::log_info("Arabic Transaction Recorded:");
			}
			else {
				success = parse_english_sms(raw_sms, record);
				if (success) hstl::log_info("English Transaction Recorded:");
			}

			if (success) {
				normalize_date(record.date);

				hstl::log_info("   Amount:   {} {}", record.amount, record.currency.view());
				hstl::log_info("   Merchant: {}", record.merchant.view());
				hstl::log_info("   Date:     {}", record.date.view());

				auto save_res = save_transaction(db, record);
				if (save_res) hstl::log_info("   -> Successfully saved to database.");
				else hstl::log_error("   -> Failed to save to database: {}", save_res.get_err().get_message());
			}
			else {
				hstl::log_warn("Ignored SMS (Not a recognized transaction format).");
			}
		}

		res.status = 200;
		res.set_content("Processed", "text/plain");
		});

	svr.Get("/api/transactions", [db](const httplib::Request&, httplib::Response& res) {
		hstl::Str json_data = get_transactions_json(db);
		res.set_content(json_data.c_str(), "application/json");
		});

	// NEW: PUT endpoint for updating transaction notes
	svr.Put(R"(/api/transactions/(\d+)/note)", [db](const httplib::Request& req, httplib::Response& res) {
		int id = std::atoi(req.matches[1].str().c_str());
		hstl::Str_View new_note(req.body.c_str(), req.body.length());

		const char* update_sql = "UPDATE transactions SET notes = ? WHERE id = ?;";
		sqlite3_stmt* stmt;

		if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) == SQLITE_OK) {
			sqlite3_bind_text(stmt, 1, new_note.data(), (int)new_note.count(), SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 2, id);

			if (sqlite3_step(stmt) == SQLITE_DONE) {
				res.status = 200;
				res.set_content("OK", "text/plain");
			}
			else {
				hstl::log_error("Note update failed: {}", sqlite3_errmsg(db));
				res.status = 500;
			}
			sqlite3_finalize(stmt);
		}
		else {
			res.status = 500;
		}
		});

	svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
		res.set_content(DASHBOARD_HTML, "text/html");
		});

	svr.Delete(R"(/api/transactions/(\d+))", [db](const httplib::Request& req, httplib::Response& res) {
		int id = std::atoi(req.matches[1].str().c_str());
		auto delete_res = delete_transaction(db, id);
		if (delete_res) {
			res.status = 200;
			res.set_content("Deleted", "text/plain");
		}
		else {
			hstl::log_error("Delete failed: {}", delete_res.get_err().get_message());
			res.status = 500;
			res.set_content("Internal Server Error", "text/plain");
		}
		});

	svr.Delete("/api/transactions", [db](const httplib::Request& req, httplib::Response& res) {
		auto clear_res = clear_transactions(db);
		if (clear_res) {
			res.status = 200;
			res.set_content("Cleared", "text/plain");
		}
		else {
			hstl::log_error("Clear failed: {}", clear_res.get_err().get_message());
			res.status = 500;
			res.set_content("Internal Server Error", "text/plain");
		}
		});

	hstl::log_info("Webhook server starting on http://0.0.0.0:8080...");
	svr.listen("0.0.0.0", 8080);

	sqlite3_close(db);

	return 0;
}