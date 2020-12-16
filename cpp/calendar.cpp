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
  recurrence_rule(RULE _interval): interval(_interval), until(NULL) { }
};

struct event {
  string title;
  tm start_time;
  tm end_time;
  time_t start_time_unix;
  time_t end_time_unix;
  bool all_day;
  bool recurrence;
  string id;
  int cal_id;
};

struct arg_t {
  vector<event>* events;
  map<string, recurrence_rule>* rules;
};

const int FLAG_ALL_DAY = (1<<3);
const int FLAG_RECURRENCE = (1<<4);

tm make_tm(int year, int month, int day) {
  tm ret;

  ret.tm_year = year - 1900;
  ret.tm_mon = month - 1;
  ret.tm_mday = day;
  ret.tm_hour = 0;
  ret.tm_min = 0;
  ret.tm_sec = 0;

  // mktime automatically fixes tm_wday and tm_yday,
  // so we do not need do calculate them
  mktime(&ret);

  return ret;
}

bool same_day(const tm& t1, const tm& t2) {
  return (t1.tm_year == t1.tm_year) && (t1.tm_yday == t2.tm_yday); 
}

bool operator<(const event& e1, const event& e2) {
  // prioritize all_day events
  if (same_day(e1.start_time, e2.start_time)) {
    if (e1.all_day)
      return true;
    else if (e2.all_day)
      return false;
  }

  return e1.start_time_unix < e2.start_time_unix;
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

string wday_name(int wday) {
  string names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"}; // a week must start from Monday
  return names[wday];
}

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
  string rule;

  for(int i = 0; i<nc; i++) {
    if (strcmp(names[i], "item_id") == 0) {
      item_id = string(columns[i]);
    }
    else if (strcmp(names[i], "icalString") == 0) {
      rule = string(columns[i]);
    }
    else {
      cout << "Fatal: should not come here (L" << __LINE__ << ")" << endl;
      return 1;
    }
  }

  // check the interval of the rule
  if (rule.find("WEEKLY") != string::npos) {
    rules->insert({item_id, recurrence_rule(recurrence_rule::WEEKLY)});
  }
  else if (rule.find("MONTHLY") != string::npos) {
    rules->insert({item_id, recurrence_rule(recurrence_rule::MONTHLY)});
  }
  else {
    cout << "Warning: recurrence rule that this program does not understand is met (L" << __LINE__ << "): " << endl
         << rule << endl;
  }

  // check if the rule expires in a future date
  if (rule.find("UNTIL") != string::npos) {
    // e.g., UNTIL=20200924T080000Z
    size_t year_start = rule.find("UNTIL") + string("UNTIL=").length();
    size_t month_start = year_start + 4;
    size_t day_start = month_start + 2;

    int year = stoi(rule.substr(year_start, 4));
    int month = stoi(rule.substr(month_start, 2));
    int day = stoi(rule.substr(day_start, 2));

    auto it = rules->find(item_id);
    if (it != rules->end()) {
      it->second.until = new tm(make_tm(year, month, day));
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

      additional_event.start_time_unix = proceed_days(&additional_event.start_time, i + 1);
      additional_event.end_time_unix = proceed_days(&additional_event.end_time, i + 1);

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
      additional_event.start_time_unix = proceed_one_week(&additional_event.start_time);
      additional_event.end_time_unix = proceed_one_week(&additional_event.end_time);

      // if there is a rule that specifies the end time, we obey it
      if (iter_rule ->second.until != NULL) {
        if (additional_event.start_time_unix > mktime(iter_rule->second.until))
          break;
      }
      // if there is no such rule, repeat for a year
      else if (i == 53) {
          break;
      }

      events->push_back(additional_event);
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
    sqlite3_exec(db, "select item_id, icalString from cal_recurrence;", parse_rules, rules, NULL);
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

    out << it->title << ",";

    out << (it->all_day ? "1" : "0") << ",";
    out << it->cal_id << endl;

    year_prev = year;
    month_prev = month;
    wday_prev = wday;
    yday_prev = yday;
  }

  return 0;
}
