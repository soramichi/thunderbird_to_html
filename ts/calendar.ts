var $ = require('jquery');
import { DateTime } from 'luxon'

function day_name(day: number): string {
    // a week should start from Monday!
    let names: string[] = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];
    return names[day];
}

interface CalDay {
    day: number;
    wday: number;
}

function make_cal(year: number, month: number): Array<CalDay> {
    const first: DateTime = DateTime.local(year, month, 1, 0, 0, 0, 0);
    const wday_first: number = first.weekday - 1; // DateTime.weekday => 1: Monday, ... 7: Sunday

    let inside: boolean = false;
    let n: number = 0;
    let ret = new Array<CalDay>();

    for(let w: number = 0; w<5; w++) {
        for(let wd: number = 0; wd<7; wd++) {
            if (wd >= wday_first && !inside)
                inside = true;
            if (n == first.daysInMonth && inside) 
                inside = false;

            if (inside) {
                // inside the month
                n++;
                ret.push({day: n, wday: wd});
            }
            else {
                // out of the month
                ret.push({day: 0, wday: wd});
            }
        }
    }

    return ret;
}

function set_color(cal_id: number, color: string): void {
    $(".cal_id_" + cal_id).css("color", color);
}

function get_data(year: number, month: number) {
    $.ajax({
        type: "GET",
        url: "./" + year + "/" + month + ".dat",
        cache: false,
        success: function(data: string) {
            const lines: Array<string> = data.split("\n");

	    lines.forEach(function(line) {
                const tokens: Array<string> = line.split(",");
                const event_date: DateTime = DateTime.fromFormat(tokens[0], "MM/dd HH:mm");

		// all-day event
                if (tokens[2] == "1")
                    $("#" + event_date.day).append("<br />" + "<span class='tag is-warning'>" + tokens[1] + "</span>");
                else {
                    $("#" + event_date.day).append("<br />" + "<span style='font-weight: bold;' class='cal_id_" + tokens[3] + "'>" + event_date.toFormat("HH:mm") + "</span> " + tokens[1]);
		}
            });

	    set_color(0, "#000080");
	    set_color(1, "#66CC00");
        }
    });
}

$(function() {
    const today: DateTime = DateTime.local();
    const param_dt = DateTime.fromFormat($(location).attr("search"), "?yyyy_MM");
    let target_month, next_month, prev_month: DateTime;

    if (param_dt.isValid)
        target_month = param_dt;
    else
        target_month = today;

    // (target_month.month + 10) % 12 + 1 == (target_month.month - 1 + 12 - 1) % 12 + 1
    // -1 : change the origin from 1 to 0
    // +12: offset by 12 so it does not become a negative value after subtracting 1
    // -1 : move to the previous month
    // %12: you know
    // +1 : rechange the origin from 0 to 1
    next_month = DateTime.local(target_month.year + (target_month.month == 12 ? 1 : 0), target_month.month % 12 + 1, 1, 0, 0, 0, 0);
    prev_month = DateTime.local(target_month.year - (target_month.month == 1  ? 1 : 0), (target_month.month + 10) % 12 + 1, 1, 0, 0, 0, 0);

    // header
    let header_str: string = "";
    header_str += ("<a href='./?" + prev_month.toFormat("yyyy_MM") + "'>&lt;" + "</a> ");
    header_str += (target_month.year + "/" + target_month.month + " ");
    header_str += ("<a href='./?" + next_month.toFormat("yyyy_MM") + "'>&gt;" + "</a> ");
    $("#header").html(header_str);

    // prepare the table
    $("#cal_body").append("<table class='table is-hoverable is-bordered'>" +
			  "<thead id='the_thead'></thead>" +
			  "<tbody id='the_tbody'></tbody>" +
			  "</table>");

    for(var i = 0; i<7; i++)
        $("#the_thead").append("<th width='14%'>" + day_name(i) + "</th>");

    const cal_days: Array<CalDay> = make_cal(target_month.year, target_month.month);
    let s: string;
    cal_days.forEach(function(cal_day) {
        if (cal_day["wday"] == 0)
	    s += "<tr>";

        if (cal_day["day"] > 0)
            s += "<td id=" + cal_day["day"] + ">" + cal_day["day"] + "</td>";
        else 
            s += "<td></td>"

        if (cal_day["wday"] == 6)
            s += "</tr>";
    });
    $("#the_tbody").append(s);

    // highlight today
    if (target_month.year == today.year && target_month.month == today.month)
	$("#" + today.day).addClass("is-selected");
    
    // popualte the contents of the table
    get_data(target_month.year, target_month.month);
})
