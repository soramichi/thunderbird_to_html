all: calendar.ts
	tsc calendar.ts
	NODE_PATH=`npm root` browserify calendar -o calendar_bundle.js
	rm -f calendar.js
