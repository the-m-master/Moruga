<a href="#">
  <img src="src/Moruga.png" style="width:200px" />
</a>


# Moruga
High Performance Data Compression

### Why the name Moruga?

The name Moruga comes from the Spanish pepper [Moruga](https://en.wikipedia.org/wiki/Trinidad_Moruga_scorpion).

I chose this name because I had two goals in mind while making it.
* The algorithm should be as fast as possible.
* The memory usage of the algorithm should be minimal.

During the creation of the implementation, it was always assumed that the memory usage (option -6) should be less than 2 GiB with an acceptable speed (about 180 seconds or so for enwik8).

And another premise was that this data compressor is not written for a specific data format or file (such as enwik8 or 9).
The benchmarks of Moruga does not rely on external preprocessor applications or dictionaries.

It must be a data compressor suitable for any data type, be it text or binary.

Adding it all up and compare it to the top of [LTCB](http://mattmahoney.net/dc/text.html), then this implementation and performance is definitely a hot pepper...

### Acknowledgements

Moruga uses the [hashtable](https://probablydance.com/2017/02/26/i-wrote-the-fastest-hashtable/) made by [Malte Skarupke](https://github.com/skarupke/flat_hash_map).

Moruga has taken advantage of ideas in the [data compression community](https://encode.su/).

Here are some of the major contributors:

* Matt Mahoney
* Alex Rhatushnyak


### Simplified architecture of Moruga

<a href="#">
  <img src="Design.svg" />
</a>

## How to build Moruga?
### Moruga was build and tested on [MSYS2](https://www.msys2.org/) and [Ubuntu LTS 20](https://ubuntu.com/)
Moruga does also build using [cygwin](https://www.cygwin.com/), but is not recommended due to the DLL hell of cygwin. Currently Moruga does not build with VS due to C++ compiler incompatibility (weak C++20 support) and lack of 128-bit variables.


For building a release version of Moruga (using [GCC](https://gcc.gnu.org/)).

```bash
make
```

To clean-up the build version

```bash
make clean
```

For building a guided release version of Moruga (using [GCC](https://gcc.gnu.org/)).
This guided release needes '[enwik8](https://cs.fit.edu/~mmahoney/compression/textdata.html)' as input file.

```bash
make guided
```

For building a debug version of Moruga (using [GCC](https://gcc.gnu.org/)).

```bash
make MODE=debug
```

To clean-up the build version

```bash
make MODE=debug clean
```

For building a release version of Moruga (using [LLVM](https://llvm.org/)).

```bash
make TOOLCHAIN=llvm
```

For building a debug version of Moruga (using [LLVM](https://llvm.org/)).

```bash
make MODE=debug TOOLCHAIN=llvm
```

Static code analysis of all CPP files of Moruga (using [LLVM](https://llvm.org/)).

```bash
make tidy
```


## Moruga enwik8 and enwik9 benchmarks

Moruga does not rely on external dictionaries or other preprocessor applications (such as DRT or enwik9-preproc).
Personally, I think this is a form of cheating or pretending your performance is better than it is.

```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| enwik8 | 100000000 | 18935263 | 18% | -0 | 73 MiB | 197.6 s | 1976 ns/sec |
| enwik8 | 100000000 | 18564490 | 18% | -1 | 101 MiB | 208.8 s | 2088 ns/sec |
| enwik8 | 100000000 | 18200187 | 18% | -2 | 157 MiB | 210.7 s | 2107 ns/sec |
| enwik8 | 100000000 | 17887387 | 17% | -3 | 269 MiB | 200.1 s | 2001 ns/sec |
| enwik8 | 100000000 | 17674152 | 17% | -4 | 494 MiB | 202.3 s | 2023 ns/sec |
| enwik8 | 100000000 | 17556068 | 17% | -5 | 943 MiB | 207.7 s | 2077 ns/sec |
| enwik8 | 100000000 | 17482518 | 17% | -6 | 1841 MiB | 214.4 s | 2144 ns/sec |
| enwik8 | 100000000 | 17473303 | 17% | -7 | 3636 MiB | 216.9 s | 2169 ns/sec |
| enwik8 | 100000000 | 17470925 | 17% | -8 | 7227 MiB | 219.9 s | 2199 ns/sec |
| enwik8 | 100000000 | 17470218 | 17% | -9 | 13383 MiB | 220.0 s | 2200 ns/sec |
| enwik9 | 1000000000 | 157889873 | 15% | -0 | 138 MiB | 1885.1 s | 1885 ns/sec |
| enwik9 | 1000000000 | 154683946 | 15% | -1 | 139 MiB | 1890.1 s | 1890 ns/sec |
| enwik9 | 1000000000 | 150982010 | 15% | -2 | 158 MiB | 1946.6 s | 1947 ns/sec |
| enwik9 | 1000000000 | 147137731 | 14% | -3 | 270 MiB | 1945.5 s | 1945 ns/sec |
| enwik9 | 1000000000 | 143567318 | 14% | -4 | 495 MiB | 1972.8 s | 1973 ns/sec |
| enwik9 | 1000000000 | 140589975 | 14% | -5 | 944 MiB | 2010.2 s | 2010 ns/sec |
| enwik9 | 1000000000 | 138430378 | 13% | -6 | 1841 MiB | 2082.1 s | 2082 ns/sec |
| enwik9 | 1000000000 | 136962606 | 13% | -7 | 3637 MiB | 2107.4 s | 2107 ns/sec |
| enwik9 | 1000000000 | 136424292 | 13% | -8 | 7228 MiB | 2151.5 s | 2151 ns/sec |
| enwik9 | 1000000000 | 136102631 | 13% | -9 | 13384 MiB | 2162.2 s | 2162 ns/sec |


### Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 29008758 | 29%
| enwik9 | 1000000000 | 253977891 | 25%


### Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 36445248 | 36%
| enwik9 | 1000000000 | 322591995 | 32%


### Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 24703772 | 24%
| enwik9 | 1000000000 | 197331816 | 19%


## Moruga [silesia](http://mattmahoney.net/dc/silesia.html) benchmarks

```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| silesia/dickens | 10192446 | 2061035 | 20% | -6 | 1841 MiB | 20.0 s | 1964 ns/sec |
| silesia/mozilla | 51220480 | 10274119 | 20% | -6 | 1857 MiB | 179.4 s | 3502 ns/sec |
| silesia/mr | 9970564 | 2106998 | 21% | -6 | 1840 MiB | 22.5 s | 2253 ns/sec |
| silesia/nci | 33553445 | 968783 | 2% | -6 | 1841 MiB | 20.6 s | 613 ns/sec |
| silesia/ooffice | 6152192 | 1738948 | 28% | -6 | 1840 MiB | 21.4 s | 3487 ns/sec |
| silesia/osdb | 10085684 | 2153740 | 21% | -6 | 1840 MiB | 36.1 s | 3577 ns/sec |
| silesia/reymont | 6627202 | 862680 | 13% | -6 | 1857 MiB | 19.4 s | 2928 ns/sec |
| silesia/samba | 21606400 | 2243028 | 10% | -6 | 1856 MiB | 72.0 s | 3331 ns/sec |
| silesia/sao | 7251944 | 4363247 | 60% | -6 | 1840 MiB | 31.4 s | 4331 ns/sec |
| silesia/webster | 41458703 | 5454425 | 13% | -6 | 1841 MiB | 84.1 s | 2028 ns/sec |
| silesia/x-ray | 8474240 | 3630174 | 42% | -6 | 1840 MiB | 28.2 s | 3330 ns/sec |
| silesia/xml | 5345280 | 307303 | 5% | -6 | 1841 MiB | 8.5 s | 1596 ns/sec |


### Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 2799520 | 27%
| silesia/mozilla | 51220480 | 17914392 | 34%
| silesia/mr | 9970564 | 2441280 | 24%
| silesia/nci | 33553445 | 1812734 | 5%
| silesia/ooffice | 6152192 | 2862526 | 46%
| silesia/osdb | 10085684 | 2802792 | 27%
| silesia/reymont | 6627202 | 1246230 | 18%
| silesia/samba | 21606400 | 4549759 | 21%
| silesia/sao | 7251944 | 4940524 | 68%
| silesia/webster | 41458703 | 8644714 | 20%
| silesia/x-ray | 8474240 | 4051112 | 47%
| silesia/xml | 5345280 | 441186 | 8%


### Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 3851823 | 37%
| silesia/mozilla | 51220480 | 18994142 | 37%
| silesia/mr | 9970564 | 3673940 | 36%
| silesia/nci | 33553445 | 2987533 | 8%
| silesia/ooffice | 6152192 | 3090442 | 50%
| silesia/osdb | 10085684 | 3716342 | 36%
| silesia/reymont | 6627202 | 1820834 | 27%
| silesia/samba | 21606400 | 5408272 | 25%
| silesia/sao | 7251944 | 5327041 | 73%
| silesia/webster | 41458703 | 12061624 | 29%
| silesia/x-ray | 8474240 | 6037713 | 71%
| silesia/xml | 5345280 | 662284 | 12%


### Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 2825584 | 27%
| silesia/mozilla | 51220480 | 13672236 | 26%
| silesia/mr | 9970564 | 2758392 | 27%
| silesia/nci | 33553445 | 1440120 | 4%
| silesia/ooffice | 6152192 | 2421724 | 39%
| silesia/osdb | 10085684 | 2861260 | 28%
| silesia/reymont | 6627202 | 1313860 | 19%
| silesia/samba | 21606400 | 3728036 | 17%
| silesia/sao | 7251944 | 4638136 | 63%
| silesia/webster | 41458703 | 8358852 | 20%
| silesia/x-ray | 8474240 | 4508396 | 53%
| silesia/xml | 5345280 | 433876 | 8%


## Moruga [calgary](https://corpus.canterbury.ac.nz/descriptions/) benchmarks


```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| calgary/bib | 111261 | 22661 | 20% | -6 | 1840 MiB | 0.8 s | 7335 ns/sec |
| calgary/book1 | 768771 | 199232 | 25% | -6 | 1840 MiB | 2.2 s | 2913 ns/sec |
| calgary/book2 | 610856 | 128661 | 21% | -6 | 1840 MiB | 1.8 s | 2918 ns/sec |
| calgary/geo | 102400 | 45750 | 44% | -6 | 1840 MiB | 0.9 s | 8609 ns/sec |
| calgary/news | 377109 | 95541 | 25% | -6 | 1840 MiB | 1.4 s | 3819 ns/sec |
| calgary/obj1 | 21504 | 8819 | 41% | -6 | 1840 MiB | 0.6 s | 25694 ns/sec |
| calgary/obj2 | 246814 | 55899 | 22% | -6 | 1840 MiB | 1.3 s | 5430 ns/sec |
| calgary/paper1 | 53161 | 14169 | 26% | -6 | 1840 MiB | 0.6 s | 11476 ns/sec |
| calgary/paper2 | 82199 | 21697 | 26% | -6 | 1840 MiB | 0.7 s | 8164 ns/sec |
| calgary/paper3 | 46526 | 13852 | 29% | -6 | 1840 MiB | 0.7 s | 14199 ns/sec |
| calgary/paper4 | 13286 | 4443 | 33% | -6 | 1840 MiB | 0.5 s | 40098 ns/sec |
| calgary/paper5 | 11954 | 4226 | 35% | -6 | 1840 MiB | 0.5 s | 44592 ns/sec |
| calgary/paper6 | 38105 | 10407 | 27% | -6 | 1840 MiB | 0.6 s | 15408 ns/sec |
| calgary/pic | 513216 | 44232 | 8% | -6 | 1840 MiB | 1.1 s | 2135 ns/sec |
| calgary/progc | 39611 | 10534 | 26% | -6 | 1840 MiB | 0.6 s | 15980 ns/sec |
| calgary/progl | 71646 | 11909 | 16% | -6 | 1840 MiB | 0.6 s | 8831 ns/sec |
| calgary/progp | 49379 | 8410 | 17% | -6 | 1840 MiB | 0.6 s | 12374 ns/sec |
| calgary/trans | 93695 | 13194 | 14% | -6 | 1840 MiB | 0.8 s | 8085 ns/sec |


## Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| calgary/bib | 111261 | 27467 | 24%
| calgary/book1 | 768771 | 232598 | 30%
| calgary/book2 | 610856 | 157443 | 25%
| calgary/geo | 102400 | 56921 | 55%
| calgary/news | 377109 | 118600 | 31%
| calgary/obj1 | 21504 | 10787 | 50%
| calgary/obj2 | 246814 | 76441 | 30%
| calgary/paper1 | 53161 | 16558 | 31%
| calgary/paper2 | 82199 | 25041 | 30%
| calgary/paper3 | 46526 | 15837 | 34%
| calgary/paper4 | 13286 | 5188 | 39%
| calgary/paper5 | 11954 | 4837 | 40%
| calgary/paper6 | 38105 | 12292 | 32%
| calgary/pic | 513216 | 49759 | 9%
| calgary/progc | 39611 | 12544 | 31%
| calgary/progl | 71646 | 15579 | 21%
| calgary/progp | 49379 | 10710 | 21%
| calgary/trans | 93695 | 17899 | 19%


## Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| calgary/bib | 111261 | 34900 | 31%
| calgary/book1 | 768771 | 312281 | 40%
| calgary/book2 | 610856 | 206158 | 33%
| calgary/geo | 102400 | 68414 | 66%
| calgary/news | 377109 | 144400 | 38%
| calgary/obj1 | 21504 | 10320 | 47%
| calgary/obj2 | 246814 | 81087 | 32%
| calgary/paper1 | 53161 | 18543 | 34%
| calgary/paper2 | 82199 | 29667 | 36%
| calgary/paper3 | 46526 | 18074 | 38%
| calgary/paper4 | 13286 | 5534 | 41%
| calgary/paper5 | 11954 | 4995 | 41%
| calgary/paper6 | 38105 | 13213 | 34%
| calgary/pic | 513216 | 52381 | 10%
| calgary/progc | 39611 | 13261 | 33%
| calgary/progl | 71646 | 16164 | 22%
| calgary/progp | 49379 | 11186 | 22%
| calgary/trans | 93695 | 18862 | 20%


## Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| calgary/bib | 111261 | 30540 | 27%
| calgary/book1 | 768771 | 260944 | 33%
| calgary/book2 | 610856 | 169580 | 27%
| calgary/geo | 102400 | 54268 | 52%
| calgary/news | 377109 | 118384 | 31%
| calgary/obj1 | 21504 | 9408 | 43%
| calgary/obj2 | 246814 | 63012 | 25%
| calgary/paper1 | 53161 | 17300 | 32%
| calgary/paper2 | 82199 | 27252 | 33%
| calgary/paper3 | 46526 | 17100 | 36%
| calgary/paper4 | 13286 | 5424 | 40%
| calgary/paper5 | 11954 | 4928 | 41%
| calgary/paper6 | 38105 | 12512 | 32%
| calgary/pic | 513216 | 39980 | 7%
| calgary/progc | 39611 | 12596 | 31%
| calgary/progl | 71646 | 14968 | 20%
| calgary/progp | 49379 | 10388 | 21%
| calgary/trans | 93695 | 16592 | 17%


## Moruga [canterbury](https://corpus.canterbury.ac.nz/descriptions/) benchmarks

```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| canterbury/alice29.txt | 152089 | 36813 | 24% | -6 | 1840 MiB | 0.8 s | 5430 ns/sec |
| canterbury/alphabet.txt | 100000 | 162 | 0% | -6 | 1840 MiB | 0.5 s | 4700 ns/sec |
| canterbury/asyoulik.txt | 125179 | 34110 | 27% | -6 | 1840 MiB | 0.8 s | 6448 ns/sec |
| canterbury/bible.txt | 4047392 | 635929 | 15% | -6 | 1840 MiB | 7.6 s | 1879 ns/sec |
| canterbury/cp.html | 24603 | 6404 | 26% | -6 | 1840 MiB | 0.5 s | 22063 ns/sec |
| canterbury/e.coli | 4638690 | 1108695 | 23% | -6 | 1841 MiB | 10.8 s | 2329 ns/sec |
| canterbury/fields.c | 11150 | 2567 | 23% | -6 | 1840 MiB | 0.5 s | 43303 ns/sec |
| canterbury/grammar.lsp | 3721 | 1053 | 28% | -6 | 1840 MiB | 0.5 s | 125067 ns/sec |
| canterbury/kennedy.xls | 1029744 | 16797 | 1% | -6 | 1840 MiB | 2.4 s | 2331 ns/sec |
| canterbury/lcet10.txt | 426754 | 88868 | 20% | -6 | 1840 MiB | 1.4 s | 3180 ns/sec |
| canterbury/pi.txt | 1000000 | 415796 | 41% | -6 | 1841 MiB | 2.9 s | 2862 ns/sec |
| canterbury/plrabn12.txt | 481861 | 125893 | 26% | -6 | 1840 MiB | 1.6 s | 3259 ns/sec |
| canterbury/ptt5 | 513216 | 44232 | 8% | -6 | 1840 MiB | 1.1 s | 2110 ns/sec |
| canterbury/random.txt | 100000 | 75346 | 75% | -6 | 1840 MiB | 0.9 s | 8915 ns/sec |
| canterbury/sum | 38240 | 9560 | 25% | -6 | 1840 MiB | 0.7 s | 17675 ns/sec |
| canterbury/xargs.1 | 4227 | 1540 | 36% | -6 | 1840 MiB | 0.5 s | 110904 ns/sec |


## Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| canterbury/alice29.txt | 152089 | 43202 | 28%
| canterbury/alphabet.txt | 100000 | 131 | 0%
| canterbury/asyoulik.txt | 125179 | 39569 | 31%
| canterbury/bible.txt | 4047392 | 845635 | 20%
| canterbury/cp.html | 24603 | 7624 | 30%
| canterbury/e.coli | 4638690 | 1251004 | 26%
| canterbury/fields.c | 11150 | 3039 | 27%
| canterbury/grammar.lsp | 3721 | 1283 | 34%
| canterbury/kennedy.xls | 1029744 | 130280 | 12%
| canterbury/lcet10.txt | 426754 | 107706 | 25%
| canterbury/pi.txt | 1000000 | 431671 | 43%
| canterbury/plrabn12.txt | 481861 | 145577 | 30%
| canterbury/ptt5 | 513216 | 49759 | 9%
| canterbury/random.txt | 100000 | 75684 | 75%
| canterbury/sum | 38240 | 12909 | 33%
| canterbury/xargs.1 | 4227 | 1762 | 41%


## Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| canterbury/alice29.txt | 152089 | 54191 | 35%
| canterbury/alphabet.txt | 100000 | 315 | 0%
| canterbury/asyoulik.txt | 125179 | 48829 | 39%
| canterbury/bible.txt | 4047392 | 1176645 | 29%
| canterbury/cp.html | 24603 | 7981 | 32%
| canterbury/e.coli | 4638690 | 1299066 | 28%
| canterbury/fields.c | 11150 | 3136 | 28%
| canterbury/grammar.lsp | 3721 | 1246 | 33%
| canterbury/kennedy.xls | 1029744 | 209733 | 20%
| canterbury/lcet10.txt | 426754 | 144429 | 33%
| canterbury/pi.txt | 1000000 | 470440 | 47%
| canterbury/plrabn12.txt | 481861 | 194277 | 40%
| canterbury/ptt5 | 513216 | 52382 | 10%
| canterbury/random.txt | 100000 | 75689 | 75%
| canterbury/sum | 38240 | 12772 | 33%
| canterbury/xargs.1 | 4227 | 1756 | 41%


## Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| canterbury/alice29.txt | 152089 | 48600 | 31%
| canterbury/alphabet.txt | 100000 | 156 | 0%
| canterbury/asyoulik.txt | 125179 | 44628 | 35%
| canterbury/bible.txt | 4047392 | 885096 | 21%
| canterbury/cp.html | 24603 | 7700 | 31%
| canterbury/e.coli | 4638690 | 1187228 | 25%
| canterbury/fields.c | 11150 | 3052 | 27%
| canterbury/grammar.lsp | 3721 | 1312 | 35%
| canterbury/kennedy.xls | 1029744 | 48016 | 4%
| canterbury/lcet10.txt | 426754 | 118948 | 27%
| canterbury/pi.txt | 1000000 | 441824 | 44%
| canterbury/plrabn12.txt | 481861 | 165336 | 34%
| canterbury/ptt5 | 513216 | 39980 | 7%
| canterbury/random.txt | 100000 | 76940 | 76%
| canterbury/sum | 38240 | 9668 | 25%
| canterbury/xargs.1 | 4227 | 1832 | 43%
