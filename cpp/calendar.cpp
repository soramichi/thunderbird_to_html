#include <cstring>
#include <ctime>

#include <iostream>
#include <vector>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <algorithm>
#include <map>

#include <sqlite3.h> 

using namespace std;

class recurrence_rule {
public:
  enum RULE { WEEKLY, MONTHLY } interval;
  tm* until;
  vector<tm*> exceptions;
  recurrence_rule(RULE _interval): interval(_interval), until(NULL) { }
  ~recurrence_rule() {
    if (until != NULL) {
      delete until;
    }
    for(auto e: exceptions) {
      delete e;
    }
  }
};

const int FLAG_ALL_DAY = (1<<3);
const int FLAG_RECURRENCE = (1<<4);

tm make_tm(int year, int month, int day) {
  tm ret{};

  ret.tm_year = year - 1900;
  ret.tm_mon = month - 1;
  ret.tm_mday = day;
  ret.tm_hour = 0;
  ret.tm_min = 0;
  ret.tm_sec = 0;
  ret.tm_isdst = 0;
  
  // mktime automatically fixes tm_wday and tm_yday,
  // so we do not need do calculate them
  mktime(&ret);

  return ret;
}

bool same_day(const tm& t1, const tm& t2) {
  return (t1.tm_year == t2.tm_year) && (t1.tm_yday == t2.tm_yday);
}

// 1. mktime() fixes overflows automatically (e.g., July 32 -> August 1).
// 2. mktime() fixes tm_wday and tm_yday automatically from the other fields.
time_t proceed_days(tm* t, int n) {
  t->tm_mday += n;
  return mktime(t);
}

time_t proceed_one_day(tm* t) {
  return proceed_days(t, 1);
}

bool is_leap_year(int y) {
  if (y % 100 == 0 && y % 400 != 0){
    return false;
  }
  else if (y % 4 == 0) {
    return true;
  }
  else {
    return false;
  }
}

time_t proceed_one_year(tm* t) {
  if (is_leap_year(t->tm_year)) {
    return proceed_days(t, 366);
  }
  else {
    return proceed_days(t, 365);
  }
}

time_t proceed_one_week(tm* t) {
  return proceed_days(t, 7);
}

time_t proceed_one_month(tm* t) {
  int month_orig = t->tm_mon;
  int day_orig = t->tm_mday;
  time_t unix_time;

  // a month is always longer than or equal to 28 days
  proceed_days(t, 28);

  // if the month proceeds by one and the day is the same, that means a month has passed
  while(!(t->tm_mon == (month_orig + 1) % 12 && t->tm_mday == day_orig)) {
    unix_time = proceed_one_day(t);
  }

  return unix_time;
}

tm icalstr_to_tm(string str, int skip = 0) {
  size_t year_start = skip;
  size_t month_start = year_start + 4;
  size_t day_start = month_start + 2;

  int year = stoi(str.substr(year_start, 4));
  int month = stoi(str.substr(month_start, 2));
  int day = stoi(str.substr(day_start, 2));

  return make_tm(year, month, day);
}

string wday_name(int wday) {
  string names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}; // a week must start from Monday
  return names[wday];
}

struct event {
  string title;
  tm start_time{};
  tm end_time{};
  time_t start_time_unix;
  time_t end_time_unix;
  bool all_day;
  bool recurrence;
  string id;
  int cal_id;

  void proceed_days(int n) {
    this->start_time_unix = ::proceed_days(&this->start_time, n);
    this->end_time_unix = ::proceed_days(&this->end_time, n);
    this->start_time = *localtime(&this->start_time_unix);
    this->end_time = *localtime(&this->end_time_unix);
  }

  void proceed_one_week() {
    this->proceed_days(7);
  }

  bool operator<(const event& rhs) const {
    // prioritize all_day events
    if (same_day(this->start_time, rhs.start_time)) {
      if (this->all_day)
	return true;
      else if (rhs.all_day)
	return false;
    }

    return this->start_time_unix < rhs.start_time_unix;
  }
};

struct arg_t {
  vector<event>* events;
  map<string, recurrence_rule>* rules;
};

sqlite3* open_db(const string& filename) {
  int rc;
  sqlite3* db;

  rc = sqlite3_open(filename.c_str(), &db);

  if(rc) {
    cerr << "Can't open database: " <<  sqlite3_errmsg(db) << endl;
    return NULL;
  }
  else {
    cerr << "Opened database successfully" << endl;
    return db;
  }
}

int parse_rules(void* _rules, int nc, char** columns, char** names) {
  map<string, recurrence_rule>* rules = reinterpret_cast<map<string, recurrence_rule>*>(_rules);
  string item_id;
  string icalString;

  for(int i = 0; i<nc; i++) {
    if (strcmp(names[i], "item_id") == 0) {
      item_id = string(columns[i]);
    }
    else if (strcmp(names[i], "icalString") == 0) {
      icalString = string(columns[i]);
    }
    else {
      cout << "Fatal: should not come here (L" << __LINE__ << ")" << endl;
      return 1;
    }
  }

  // check the interval of the icalString
  if (icalString.find("WEEKLY") != string::npos) {
    rules->insert({item_id, recurrence_rule(recurrence_rule::WEEKLY)});
  }
  else if (icalString.find("MONTHLY") != string::npos) {
    rules->insert({item_id, recurrence_rule(recurrence_rule::MONTHLY)});
  }
  // find the exceptional dates
  else if (icalString.find("EXDATE") != string::npos) {
    tm exdate_tm = icalstr_to_tm(icalString, icalString.find("EXDATE") + string("EXDATE;").length());

    auto rule = rules->find(item_id);
    if (rule == rules->end()) {
      cerr << "Fatal: an exception rule for an event that is not yet parsed is found!" << endl;
      exit(1);
    }
    else {
      rule->second.exceptions.push_back(new tm(exdate_tm));
    }
  }

  // check if the rule expires in a future date
  if (icalString.find("UNTIL") != string::npos) {
    // icalString: XXXX; UNTIL=20200924T080000
    tm until_tm = icalstr_to_tm(icalString, icalString.find("UNTIL") + string("UNTIL=").length());
    auto it = rules->find(item_id);
    if (it != rules->end()) {
      it->second.until = new tm(until_tm);
    }
    else {
      // should not come here
      cout << "Fatal: trying to interpret the 'UNTIL' parameter for a not-yet-inserted rule (L" << __LINE__ << ")" << endl;
      return 1;
    }
  } 
  
  return 0;
}

int parse_results(void* _arg, int nc, char** columns, char** names) {
  arg_t* arg = reinterpret_cast<arg_t*>(_arg);
  vector<event>* events = arg->events;
  map<string, recurrence_rule>* rules = arg->rules;

  static map<string, int> cal_ids;
  static int cal_id;

  event new_event;

  for(int i = 0; i<nc; i++) {
    if (strcmp(names[i], "event_start") == 0 || strcmp(names[i], "event_end") == 0) {
      time_t time_from_epoch;

      // "event_start" and "event_end" include some extra information
      // in addition to unix time, so just chop it to 10 digits      
      time_from_epoch = stoul(string(columns[i]).substr(0, 10));

      if (strcmp(names[i], "event_start") == 0) {
	localtime_r(&time_from_epoch, &new_event.start_time);
        new_event.start_time_unix = time_from_epoch;
      }
      else {
	localtime_r(&time_from_epoch, &new_event.end_time);
        new_event.end_time_unix = time_from_epoch;
      }
    }
    else if (strcmp(names[i], "title") == 0) {
      new_event.title = string(columns[i]);
    }
    else if (strcmp(names[i], "flags") == 0) {
      int flags = stoi(string(columns[i]));
      new_event.all_day = (flags & FLAG_ALL_DAY);
      new_event.recurrence = (flags & FLAG_RECURRENCE);
    }
    else if (strcmp(names[i], "id") == 0) {
      new_event.id = string(columns[i]);
    }
    else if (strcmp(names[i], "cal_id") == 0) {
      if (cal_ids.find(string(columns[i])) == cal_ids.end()) {
	cal_ids[string(columns[i])] = cal_id++;
      }

      new_event.cal_id = cal_ids[string(columns[i])];
    }
    else {
      // should not come here
      cout << "Fatal: should not come here (L:" << __LINE__ << ")" << endl;
      return 1;
    }
  }

  // sqlite3_exec should call only one instance of this callback at a time
  // otherwise this is a serious data race!
  events->push_back(new_event);

  if (new_event.all_day) {
    // -1 because a 1-day long event spans across 2 days in the thunderbird's sementics
    for(int i = 0; i < new_event.end_time.tm_mday - new_event.start_time.tm_mday - 1; i++) {
      event additional_event = new_event;
      additional_event.proceed_days(i + 1);
      events->push_back(additional_event);
    }
  }

  // Note: even if there exists a recurrence rule that matches the id of `new_event',
  // we have to check `new_event.recurrence' before applying the rule
  // because thunderbird may create events with the same exact id to manage exceptions
  // for a recurrent event (e.g., every Monday, but Tuesday for a particular week)
  auto iter_rule = rules->find(new_event.id);
  if (new_event.recurrence && iter_rule != rules->end() && iter_rule->second.interval == recurrence_rule::WEEKLY) {
    event additional_event = new_event;

    for(int i = 0; ; i++) {
      additional_event.proceed_one_week();
 
      // if there is a rule that specifies the end time, we obey it
      if (iter_rule->second.until != NULL) {
	// "UNTIL" in icalString points to 00:00:00 in the day to which the last event belongs,
	// so we have to compare the beginning of the next day of "UNTIL" and `additional_event'
	if (additional_event.start_time_unix >= proceed_one_day(iter_rule->second.until))
          break;
      }
      // if there is no such rule, repeat for a year
      else if (i == 53) {
          break;
      }

      // if the date to be added is in the exceptions list, skip it
      bool skip = false;
      for(const auto& ex: iter_rule->second.exceptions) {
	if(same_day(additional_event.start_time, *ex)) {
	  skip = true;
	}
      }

      if (!skip) {
	events->push_back(additional_event);
      }
    }
  }

  return 0;
}

int main(int argc, char* argv[]) {
  sqlite3* db = open_db("local.sqlite");
  vector<event>* events = new vector<event>();
  map<string, recurrence_rule>* rules = new map<string, recurrence_rule>();

  arg_t arg = { events, rules };

  if (db != NULL) {
    sqlite3_limit(db, SQLITE_LIMIT_WORKER_THREADS, 1);
    sqlite3_exec(db, "select item_id, icalString from cal_recurrence;", parse_rules, rules, NULL);
    sqlite3_limit(db, SQLITE_LIMIT_WORKER_THREADS, 1);
    sqlite3_exec(db, "select cal_id, id, title, event_start, event_end, flags from cal_events;", parse_results, &arg, NULL);
    sqlite3_close(db);
  }
  else {
    // error, do nothing
    return -1;
  }

  sort(events->begin(), events->end());

  int year_prev = -1, month_prev = -1, wday_prev = -1, yday_prev = -1;
  ofstream out;

  // this for loop assumes that events are ordered by their start_time
  for(auto it = events->begin(); it != events->end(); it++) {
    int year = it->start_time.tm_year + 1900;
    int month = it->start_time.tm_mon + 1;
    int wday = (it->start_time.tm_wday + 7 - 1) % 7; // tm_wday starts from Sunday, but I want a week to start from Monday
    int yday = it->start_time.tm_yday;

    // first event of this month
    if (year != year_prev || month != month_prev) {
      stringstream ss;

      ss << "data/" << year;
      filesystem::create_directories(ss.str());

      // close the stream for month_prev
      if (month_prev != -1) {
        out.close();
      }

      // make new filestream for this month
      ss << "/" << month << ".dat";
      out = ofstream(ss.str());

      if (!out) {
	cerr << "fatal: cannot open file: " << ss.str() << endl;
      }
    }

    out << setw(2) << setfill('0') << month << "/"
        << setw(2) << setfill('0') << it->start_time.tm_mday << " ";
    
    out << setw(2) << setfill('0') << it->start_time.tm_hour << ":"
        << setw(2) << setfill('0') << it->start_time.tm_min << ",";

    out << (it->all_day ? "1" : "0") << ",";
    out << it->cal_id << ",";
    out << it->title << endl;

    year_prev = year;
    month_prev = month;
    wday_prev = wday;
    yday_prev = yday;
  }

  sqlite3_close(db);
  delete events;
  delete rules;

  return 0;
}
