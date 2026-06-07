#!/bin/sh
set -eu

fail() {
	printf 'FAIL: %s\n' "$1" >&2
	exit 1
}

if grep -Fq "TVA non applicable, art. 293 B du CGI" src/db.c; then
	fail "the company legal notice must not default to the VAT exemption notice"
fi

grep -Fq "VALUES(1,'','','','','','');" src/db.c ||
	fail "the company legal notice must default to an empty string"

grep -Fq "UPDATE company SET legal='' WHERE id=1" src/db.c ||
	fail "existing databases with the old VAT exemption default must be migrated"

grep -Fq "gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->line_vat),20);" src/main.c ||
	fail "new invoice lines must default to 20% VAT"

printf 'VAT defaults: OK\n'
