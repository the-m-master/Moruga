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

When creating this implementation, it was assumed that the memory usage (option -6) should be less than 2 GiB at an acceptable speed (about 250 seconds or so, for enwik8 on Ryzen 9 3950X).

And another premise was that this data compressor is not written for a specific data format or file (like enwik8 or 9).
Moruga's benchmarks do not rely on external preprocessor applications or (external) dictionaries (such as DRT or enwik9-preproc).
Personally, I think this is a form of cheating or pretending your performance is better than it is.

It must be a data compressor suitable for any data type, be it text or binary.

Adding it all up and compare it to the top of [LTCB](http://mattmahoney.net/dc/text.html), then this implementation and performance is definitely a hot pepper...

### Acknowledgements

Moruga uses the [hashtable](https://probablydance.com/2017/02/26/i-wrote-the-fastest-hashtable/) made by [Malte Skarupke](https://github.com/skarupke/flat_hash_map).

Moruga has taken advantage of ideas in the [data compression community](https://encode.su/).

Here are some of the major contributors:

* Matt Mahoney
* Alexander Rhatushnyak


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


```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| enwik8 | 100000000 | 19086431 | 19% | -0 | 82 MiB | 175.5 s | 1755 ns/B |
| enwik8 | 100000000 | 18628321 | 18% | -1 | 114 MiB | 185.9 s | 1859 ns/B |
| enwik8 | 100000000 | 18189800 | 18% | -2 | 177 MiB | 198.6 s | 1986 ns/B |
| enwik8 | 100000000 | 17818937 | 17% | -3 | 302 MiB | 210.3 s | 2103 ns/B |
| enwik8 | 100000000 | 17559022 | 17% | -4 | 553 MiB | 222.7 s | 2227 ns/B |
| enwik8 | 100000000 | 17405186 | 17% | -5 | 1056 MiB | 235.8 s | 2358 ns/B |
| enwik8 | 100000000 | 17302917 | 17% | -6 | 1933 MiB | 249.1 s | 2491 ns/B |
| enwik8 | 100000000 | 17269577 | 17% | -7 | 3686 MiB | 257.8 s | 2578 ns/B |
| enwik8 | 100000000 | 17250068 | 17% | -8 | 7193 MiB | 265.6 s | 2656 ns/B |
| enwik8 | 100000000 | 17239773 | 17% | -9 | 14207 MiB | 274.3 s | 2743 ns/B |
| enwik8 | 100000000 | 17233511 | 17% | -10 | 27208 MiB | 290.5 s | 2905 ns/B |
| enwik9 | 1000000000 | 159793250 | 15% | -0 | 139 MiB | 1684.6 s | 1685 ns/B |
| enwik9 | 1000000000 | 155349835 | 15% | -1 | 139 MiB | 1805.8 s | 1806 ns/B |
| enwik9 | 1000000000 | 150738632 | 15% | -2 | 177 MiB | 1934.3 s | 1934 ns/B |
| enwik9 | 1000000000 | 146227049 | 14% | -3 | 303 MiB | 2044.2 s | 2044 ns/B |
| enwik9 | 1000000000 | 142175884 | 14% | -4 | 554 MiB | 2155.6 s | 2156 ns/B |
| enwik9 | 1000000000 | 138858401 | 13% | -5 | 1056 MiB | 2273.3 s | 2273 ns/B |
| enwik9 | 1000000000 | 136385659 | 13% | -6 | 2061 MiB | 2405.6 s | 2406 ns/B |
| enwik9 | 1000000000 | 134860168 | 13% | -7 | 4071 MiB | 2498.5 s | 2499 ns/B |
| enwik9 | 1000000000 | 134115580 | 13% | -8 | 8091 MiB | 2576.0 s | 2576 ns/B |
| enwik9 | 1000000000 | 133650977 | 13% | -9 | 15105 MiB | 2642.4 s | 2642 ns/B |
| enwik9 | 1000000000 | 133487783 | 13% | -10 | 28106 MiB | 2739.9 s | 2740 ns/B |


### Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 29008758 | 29% |
| enwik9 | 1000000000 | 253977891 | 25% |


### Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 36445248 | 36% |
| enwik9 | 1000000000 | 322591995 | 32% |


### Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| enwik8 | 100000000 | 24703772 | 24% |
| enwik9 | 1000000000 | 197331816 | 19% |


## Moruga [silesia](http://mattmahoney.net/dc/silesia.html) benchmarks

```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| silesia/dickens | 10192446 | 2056441 | 20% | -6 | 1821 MiB | 23.4 s | 2297 ns/B |
| silesia/mozilla | 51220480 | 10147370 | 19% | -6 | 1885 MiB | 204.5 s | 3993 ns/B |
| silesia/mr | 9970564 | 2097001 | 21% | -6 | 1820 MiB | 24.2 s | 2428 ns/B |
| silesia/nci | 33553445 | 953088 | 2% | -6 | 1837 MiB | 23.2 s | 691 ns/B |
| silesia/ooffice | 6152192 | 1729769 | 28% | -6 | 1812 MiB | 25.4 s | 4129 ns/B |
| silesia/osdb | 10085684 | 2143927 | 21% | -6 | 1820 MiB | 42.3 s | 4191 ns/B |
| silesia/reymont | 6627202 | 853580 | 12% | -6 | 1829 MiB | 23.0 s | 3469 ns/B |
| silesia/samba | 21606400 | 2218019 | 10% | -6 | 1852 MiB | 81.5 s | 3771 ns/B |
| silesia/sao | 7251944 | 4387394 | 60% | -6 | 1812 MiB | 35.9 s | 4945 ns/B |
| silesia/webster | 41458703 | 5379249 | 12% | -6 | 1868 MiB | 99.6 s | 2403 ns/B |
| silesia/x-ray | 8474240 | 3621024 | 42% | -6 | 1820 MiB | 32.6 s | 3848 ns/B |
| silesia/xml | 5345280 | 305092 | 5% | -6 | 1812 MiB | 10.1 s | 1887 ns/B |


### Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 2799520 | 27% |
| silesia/mozilla | 51220480 | 17914392 | 34% |
| silesia/mr | 9970564 | 2441280 | 24% |
| silesia/nci | 33553445 | 1812734 | 5% |
| silesia/ooffice | 6152192 | 2862526 | 46% |
| silesia/osdb | 10085684 | 2802792 | 27% |
| silesia/reymont | 6627202 | 1246230 | 18% |
| silesia/samba | 21606400 | 4549759 | 21% |
| silesia/sao | 7251944 | 4940524 | 68% |
| silesia/webster | 41458703 | 8644714 | 20% |
| silesia/x-ray | 8474240 | 4051112 | 47% |
| silesia/xml | 5345280 | 441186 | 8% |


### Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 3851823 | 37% |
| silesia/mozilla | 51220480 | 18994142 | 37% |
| silesia/mr | 9970564 | 3673940 | 36% |
| silesia/nci | 33553445 | 2987533 | 8% |
| silesia/ooffice | 6152192 | 3090442 | 50% |
| silesia/osdb | 10085684 | 3716342 | 36% |
| silesia/reymont | 6627202 | 1820834 | 27% |
| silesia/samba | 21606400 | 5408272 | 25% |
| silesia/sao | 7251944 | 5327041 | 73% |
| silesia/webster | 41458703 | 12061624 | 29% |
| silesia/x-ray | 8474240 | 6037713 | 71% |
| silesia/xml | 5345280 | 662284 | 12% |


### Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| silesia/dickens | 10192446 | 2825584 | 27% |
| silesia/mozilla | 51220480 | 13672236 | 26% |
| silesia/mr | 9970564 | 2758392 | 27% |
| silesia/nci | 33553445 | 1440120 | 4% |
| silesia/ooffice | 6152192 | 2421724 | 39% |
| silesia/osdb | 10085684 | 2861260 | 28% |
| silesia/reymont | 6627202 | 1313860 | 19% |
| silesia/samba | 21606400 | 3728036 | 17% |
| silesia/sao | 7251944 | 4638136 | 63% |
| silesia/webster | 41458703 | 8358852 | 20% |
| silesia/x-ray | 8474240 | 4508396 | 53% |
| silesia/xml | 5345280 | 433876 | 8% |


## Moruga [calgary](https://corpus.canterbury.ac.nz/descriptions/) benchmarks


```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| calgary/bib | 111261 | 22612 | 20% | -6 | 1804 MiB | 1.0 s | 9248 ns/B |
| calgary/book1 | 768771 | 198579 | 25% | -6 | 1805 MiB | 2.8 s | 3601 ns/B |
| calgary/book2 | 610856 | 128101 | 20% | -6 | 1805 MiB | 2.3 s | 3723 ns/B |
| calgary/geo | 102400 | 45819 | 44% | -6 | 1804 MiB | 1.1 s | 10915 ns/B |
| calgary/news | 377109 | 94951 | 25% | -6 | 1805 MiB | 1.9 s | 4997 ns/B |
| calgary/obj1 | 21504 | 9158 | 42% | -6 | 1804 MiB | 0.7 s | 30852 ns/B |
| calgary/obj2 | 246814 | 56049 | 22% | -6 | 1804 MiB | 1.7 s | 6743 ns/B |
| calgary/paper1 | 53161 | 14301 | 26% | -6 | 1804 MiB | 0.8 s | 14389 ns/B |
| calgary/paper2 | 82199 | 21755 | 26% | -6 | 1804 MiB | 0.9 s | 10887 ns/B |
| calgary/paper3 | 46526 | 13894 | 29% | -6 | 1804 MiB | 0.8 s | 16713 ns/B |
| calgary/paper4 | 13286 | 4513 | 33% | -6 | 1804 MiB | 0.5 s | 38318 ns/B |
| calgary/paper5 | 11954 | 4335 | 36% | -6 | 1804 MiB | 0.5 s | 41965 ns/B |
| calgary/paper6 | 38105 | 10589 | 27% | -6 | 1804 MiB | 0.7 s | 17557 ns/B |
| calgary/pic | 513216 | 44295 | 8% | -6 | 1805 MiB | 1.4 s | 2672 ns/B |
| calgary/progc | 39611 | 10662 | 26% | -6 | 1804 MiB | 0.8 s | 19083 ns/B |
| calgary/progl | 71646 | 11971 | 16% | -6 | 1804 MiB | 0.8 s | 10479 ns/B |
| calgary/progp | 49379 | 8614 | 17% | -6 | 1804 MiB | 0.7 s | 13189 ns/B |
| calgary/trans | 93695 | 13415 | 14% | -6 | 1804 MiB | 0.9 s | 9266 ns/B |


## Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| calgary/bib | 111261 | 27467 | 24% |
| calgary/book1 | 768771 | 232598 | 30% |
| calgary/book2 | 610856 | 157443 | 25% |
| calgary/geo | 102400 | 56921 | 55% |
| calgary/news | 377109 | 118600 | 31% |
| calgary/obj1 | 21504 | 10787 | 50% |
| calgary/obj2 | 246814 | 76441 | 30% |
| calgary/paper1 | 53161 | 16558 | 31% |
| calgary/paper2 | 82199 | 25041 | 30% |
| calgary/paper3 | 46526 | 15837 | 34% |
| calgary/paper4 | 13286 | 5188 | 39% |
| calgary/paper5 | 11954 | 4837 | 40% |
| calgary/paper6 | 38105 | 12292 | 32% |
| calgary/pic | 513216 | 49759 | 9% |
| calgary/progc | 39611 | 12544 | 31% |
| calgary/progl | 71646 | 15579 | 21% |
| calgary/progp | 49379 | 10710 | 21% |
| calgary/trans | 93695 | 17899 | 19% |


## Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| calgary/bib | 111261 | 34900 | 31% |
| calgary/book1 | 768771 | 312281 | 40% |
| calgary/book2 | 610856 | 206158 | 33% |
| calgary/geo | 102400 | 68414 | 66% |
| calgary/news | 377109 | 144400 | 38% |
| calgary/obj1 | 21504 | 10320 | 47% |
| calgary/obj2 | 246814 | 81087 | 32% |
| calgary/paper1 | 53161 | 18543 | 34% |
| calgary/paper2 | 82199 | 29667 | 36% |
| calgary/paper3 | 46526 | 18074 | 38% |
| calgary/paper4 | 13286 | 5534 | 41% |
| calgary/paper5 | 11954 | 4995 | 41% |
| calgary/paper6 | 38105 | 13213 | 34% |
| calgary/pic | 513216 | 52381 | 10% |
| calgary/progc | 39611 | 13261 | 33% |
| calgary/progl | 71646 | 16164 | 22% |
| calgary/progp | 49379 | 11186 | 22% |
| calgary/trans | 93695 | 18862 | 20% |


## Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| calgary/bib | 111261 | 30540 | 27% |
| calgary/book1 | 768771 | 260944 | 33% |
| calgary/book2 | 610856 | 169580 | 27% |
| calgary/geo | 102400 | 54268 | 52% |
| calgary/news | 377109 | 118384 | 31% |
| calgary/obj1 | 21504 | 9408 | 43% |
| calgary/obj2 | 246814 | 63012 | 25% |
| calgary/paper1 | 53161 | 17300 | 32% |
| calgary/paper2 | 82199 | 27252 | 33% |
| calgary/paper3 | 46526 | 17100 | 36% |
| calgary/paper4 | 13286 | 5424 | 40% |
| calgary/paper5 | 11954 | 4928 | 41% |
| calgary/paper6 | 38105 | 12512 | 32% |
| calgary/pic | 513216 | 39980 | 7% |
| calgary/progc | 39611 | 12596 | 31% |
| calgary/progl | 71646 | 14968 | 20% |
| calgary/progp | 49379 | 10388 | 21% |
| calgary/trans | 93695 | 16592 | 17% |


## Moruga [canterbury](https://corpus.canterbury.ac.nz/descriptions/) benchmarks

```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| canterbury/alice29.txt | 152089 | 36817 | 24% | -6 | 1804 MiB | 1.1 s | 7384 ns/B |
| canterbury/alphabet.txt | 100000 | 175 | 0% | -6 | 1804 MiB | 0.4 s | 4359 ns/B |
| canterbury/asyoulik.txt | 125179 | 34119 | 27% | -6 | 1804 MiB | 1.1 s | 8501 ns/B |
| canterbury/bible.txt | 4047392 | 634091 | 15% | -6 | 1809 MiB | 8.7 s | 2143 ns/B |
| canterbury/cp.html | 24603 | 6493 | 26% | -6 | 1804 MiB | 0.6 s | 24005 ns/B |
| canterbury/e.coli | 4638690 | 1106837 | 23% | -6 | 1813 MiB | 11.5 s | 2482 ns/B |
| canterbury/fields.c | 11150 | 2676 | 24% | -6 | 1804 MiB | 0.5 s | 45561 ns/B |
| canterbury/grammar.lsp | 3721 | 1132 | 30% | -6 | 1804 MiB | 0.4 s | 115506 ns/B |
| canterbury/kennedy.xls | 1029744 | 14806 | 1% | -6 | 1805 MiB | 2.8 s | 2740 ns/B |
| canterbury/lcet10.txt | 426754 | 88702 | 20% | -6 | 1805 MiB | 1.8 s | 4118 ns/B |
| canterbury/pi.txt | 1000000 | 415606 | 41% | -6 | 1805 MiB | 3.0 s | 3048 ns/B |
| canterbury/plrabn12.txt | 481861 | 125527 | 26% | -6 | 1805 MiB | 2.1 s | 4306 ns/B |
| canterbury/ptt5 | 513216 | 44295 | 8% | -6 | 1805 MiB | 1.4 s | 2663 ns/B |
| canterbury/random.txt | 100000 | 75419 | 75% | -6 | 1804 MiB | 3.7 s | 36999 ns/B |
| canterbury/sum | 38240 | 9755 | 25% | -6 | 1804 MiB | 3.9 s | 102822 ns/B |
| canterbury/xargs.1 | 4227 | 1613 | 38% | -6 | 1804 MiB | 4.3 s | 1007758 ns/B |


## Benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| canterbury/alice29.txt | 152089 | 43202 | 28% |
| canterbury/alphabet.txt | 100000 | 131 | 0% |
| canterbury/asyoulik.txt | 125179 | 39569 | 31% |
| canterbury/bible.txt | 4047392 | 845635 | 20% |
| canterbury/cp.html | 24603 | 7624 | 30% |
| canterbury/e.coli | 4638690 | 1251004 | 26% |
| canterbury/fields.c | 11150 | 3039 | 27% |
| canterbury/grammar.lsp | 3721 | 1283 | 34% |
| canterbury/kennedy.xls | 1029744 | 130280 | 12% |
| canterbury/lcet10.txt | 426754 | 107706 | 25% |
| canterbury/pi.txt | 1000000 | 431671 | 43% |
| canterbury/plrabn12.txt | 481861 | 145577 | 30% |
| canterbury/ptt5 | 513216 | 49759 | 9% |
| canterbury/random.txt | 100000 | 75684 | 75% |
| canterbury/sum | 38240 | 12909 | 33% |
| canterbury/xargs.1 | 4227 | 1762 | 41% |


## Benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| canterbury/alice29.txt | 152089 | 54191 | 35% |
| canterbury/alphabet.txt | 100000 | 315 | 0% |
| canterbury/asyoulik.txt | 125179 | 48829 | 39% |
| canterbury/bible.txt | 4047392 | 1176645 | 29% |
| canterbury/cp.html | 24603 | 7981 | 32% |
| canterbury/e.coli | 4638690 | 1299066 | 28% |
| canterbury/fields.c | 11150 | 3136 | 28% |
| canterbury/grammar.lsp | 3721 | 1246 | 33% |
| canterbury/kennedy.xls | 1029744 | 209733 | 20% |
| canterbury/lcet10.txt | 426754 | 144429 | 33% |
| canterbury/pi.txt | 1000000 | 470440 | 47% |
| canterbury/plrabn12.txt | 481861 | 194277 | 40% |
| canterbury/ptt5 | 513216 | 52382 | 10% |
| canterbury/random.txt | 100000 | 75689 | 75% |
| canterbury/sum | 38240 | 12772 | 33% |
| canterbury/xargs.1 | 4227 | 1756 | 41% |


## Benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| canterbury/alice29.txt | 152089 | 48600 | 31% |
| canterbury/alphabet.txt | 100000 | 156 | 0% |
| canterbury/asyoulik.txt | 125179 | 44628 | 35% |
| canterbury/bible.txt | 4047392 | 885096 | 21% |
| canterbury/cp.html | 24603 | 7700 | 31% |
| canterbury/e.coli | 4638690 | 1187228 | 25% |
| canterbury/fields.c | 11150 | 3052 | 27% |
| canterbury/grammar.lsp | 3721 | 1312 | 35% |
| canterbury/kennedy.xls | 1029744 | 48016 | 4% |
| canterbury/lcet10.txt | 426754 | 118948 | 27% |
| canterbury/pi.txt | 1000000 | 441824 | 44% |
| canterbury/plrabn12.txt | 481861 | 165336 | 34% |
| canterbury/ptt5 | 513216 | 39980 | 7% |
| canterbury/random.txt | 100000 | 76940 | 76% |
| canterbury/sum | 38240 | 9668 | 25% |
| canterbury/xargs.1 | 4227 | 1832 | 43% |

## Moruga [misguided](http://mattmahoney.net/dc/barf.html) benchmarks

Using DRT improves encoder results.
Decoding requires the use of DRT (15548 bytes on Windows, 20824 bytes on Linux), including the dictionary (lpqdict0.dic is 465210 bytes).

Using enwik9-preproc will improve the encoder results.
Decoding requires the use of enwik9-preproc (about 52 KiB in size on Windows), including the .new_article_order file (1131233 bytes in size).

I believe that these figures should somehow be included in the benchmarks. Along with the extra CPU and RAM usage of these tools.

```bash
Moruga c -6 <infile> <outfile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| misguided/enwik8.drt | 60824424 | 17190996 | 28% |
| misguided/enwik9.drt | 639386659 | 136658041 | 21% |
| misguided/ready4cmix | 934188796 | 130880749 | 14% |


### Misguided benchmarks compared to BZIP2

```bash
bzip2 --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| misguided/enwik8.drt | 60824424 | 17261149 | 28% |
| misguided/enwik9.drt | 639386659 | 137549735 | 21% |
| misguided/ready4cmix | 934188796 | 131410411 | 14% |


### Misguided benchmarks compared to GZIP

```bash
gzip --best <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| misguided/enwik8.drt | 60824424 | 30749706 | 50% |
| misguided/enwik9.drt | 639386659 | 275683807 | 43% |
| misguided/ready4cmix | 934188796 | 296084932 | 31% |


### Misguided benchmarks compared to XZ

```bash
xz --lzma2=preset=9e,dict=1GiB,lc=4,pb=0 <infile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| misguided/enwik8.drt | 60824424 | 22598324 | 37% |
| misguided/enwik9.drt | 639386659 | 183529616 | 28% |
| misguided/ready4cmix | 934188796 | 189196884 | 20% |
