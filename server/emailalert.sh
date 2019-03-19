#! /bin/sh
# argument 1 contains e-mail address to send alert to, argument 2 and on contains message content
echo Sending email alert to $1...
mail -s "LN2 System Alert" $1 <<**
$2 $3 $4 $5 $6 $7 $8 $9 ${10} ${11} ${12} ${13} ${14} ${15} ${16} ${17} ${18} ${19} ${20} ${21} ${22} ${23} ${24} ${25} ${26} ${27} ${28} ${29} ${30} ${31} ${32}

This is an automated message sent from the LN2@c7076-gears.chem.sfu.ca .  Please do not respond.
**
echo Alert sent!
