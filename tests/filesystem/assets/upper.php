<?php

do {
    echo strtoupper((string) fread(STDIN, 1024));
} while (!feof(STDIN));
