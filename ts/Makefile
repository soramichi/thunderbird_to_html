all: calendar.ts
	./node_modules/typescript/bin/tsc calendar.ts
	./node_modules/browserify/bin/cmd.js calendar -o calendar_bundle.js
	rm -f calendar.js
	./node_modules/uglify-js/bin/uglifyjs --compress --mangle  -- calendar_bundle.js > calendar_bundle.min.js
