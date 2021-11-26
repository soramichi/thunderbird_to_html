#!/bin/bash

# config
s=`cat s.txt`
u=`cat u.txt`
p=`cat p.txt`
mkdir deploy

# upload the static files
cp html/index.html deploy
cp ts/calendar_bundle.min.js deploy
cp css/mybulma/css/mystyles.css deploy

ftp -n << EOF
open $s
user $u $p
passive
ascii
prompt
cd www/cal
lcd deploy
mput index.html calendar_bundle.min.js mystyles.css
quit
EOF

# upload the data files
diff cpp/local.sqlite /nas/thunderbird/ib5vsgs6.shared/calendar-data/local.sqlite
if [ "$?" -eq "0" ]; then
    echo "the local database is up-to-date."
    exit
fi

./create_data.sh
cp -r cpp/data deploy

for y in `ls -1 ./deploy/data`; do
    echo $y
    sleep 10
ftp -n << EOF
open $s
user $u $p
passive
ascii
prompt
cd www/cal
mkdir $y
cd $y
lcd deploy/data/$y
mput *.dat
quit 
EOF
done
