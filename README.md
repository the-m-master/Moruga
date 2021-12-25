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
| enwik8 | 100000000 | 18932494 | 18% | -0 | 73 MiB | 194.5 s | 1945 ns/sec |
| enwik8 | 100000000 | 18559778 | 18% | -1 | 101 MiB | 201.1 s | 2011 ns/sec |
| enwik8 | 100000000 | 18195621 | 18% | -2 | 157 MiB | 202.1 s | 2021 ns/sec |
| enwik8 | 100000000 | 17883240 | 17% | -3 | 270 MiB | 197.0 s | 1970 ns/sec |
| enwik8 | 100000000 | 17669643 | 17% | -4 | 494 MiB | 198.7 s | 1987 ns/sec |
| enwik8 | 100000000 | 17551944 | 17% | -5 | 943 MiB | 203.0 s | 2030 ns/sec |
| enwik8 | 100000000 | 17478866 | 17% | -6 | 1841 MiB | 210.7 s | 2107 ns/sec |
| enwik8 | 100000000 | 17469676 | 17% | -7 | 3636 MiB | 212.7 s | 2127 ns/sec |
| enwik8 | 100000000 | 17467284 | 17% | -8 | 7227 MiB | 215.0 s | 2150 ns/sec |
| enwik8 | 100000000 | 17466538 | 17% | -9 | 13383 MiB | 218.6 s | 2186 ns/sec |
| enwik9 | 1000000000 | 157866638 | 15% | -0 | 138 MiB | 1839.1 s | 1839 ns/sec |
| enwik9 | 1000000000 | 154513252 | 15% | -1 | 140 MiB | 1922.2 s | 1922 ns/sec |
| enwik9 | 1000000000 | 150943472 | 15% | -2 | 158 MiB | 1894.9 s | 1895 ns/sec |
| enwik9 | 1000000000 | 147088827 | 14% | -3 | 270 MiB | 1896.0 s | 1896 ns/sec |
| enwik9 | 1000000000 | 143527682 | 14% | -4 | 494 MiB | 1922.9 s | 1923 ns/sec |
| enwik9 | 1000000000 | 140564094 | 14% | -5 | 943 MiB | 1964.9 s | 1965 ns/sec |
| enwik9 | 1000000000 | 138196520 | 13% | -6 | 1841 MiB | 2027.8 s | 2028 ns/sec |
| enwik9 | 1000000000 | 136929808 | 13% | -7 | 3637 MiB | 2078.7 s | 2079 ns/sec |
| enwik9 | 1000000000 | 136393680 | 13% | -8 | 7228 MiB | 2106.8 s | 2107 ns/sec |
| enwik9 | 1000000000 | 136064646 | 13% | -9 | 13383 MiB | 2123.8 s | 2124 ns/sec |


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
| silesia/dickens | 10192446 | 2061270 | 20% | -6 | 1841 MiB | 22.8 s | 2237 ns/sec |
| silesia/mozilla | 51220480 | 10274176 | 20% | -6 | 1857 MiB | 191.4 s | 3737 ns/sec |
| silesia/mr | 9970564 | 2107122 | 21% | -6 | 1840 MiB | 23.3 s | 2333 ns/sec |
| silesia/nci | 33553445 | 968664 | 2% | -6 | 1841 MiB | 22.1 s | 659 ns/sec |
| silesia/ooffice | 6152192 | 1738798 | 28% | -6 | 1840 MiB | 23.0 s | 3733 ns/sec |
| silesia/osdb | 10085684 | 2153684 | 21% | -6 | 1840 MiB | 39.8 s | 3942 ns/sec |
| silesia/reymont | 6627202 | 862628 | 13% | -6 | 1857 MiB | 21.3 s | 3210 ns/sec |
| silesia/samba | 21606400 | 2243044 | 10% | -6 | 1856 MiB | 77.8 s | 3601 ns/sec |
| silesia/sao | 7251944 | 4363149 | 60% | -6 | 1840 MiB | 34.5 s | 4762 ns/sec |
| silesia/webster | 41458703 | 5453673 | 13% | -6 | 1841 MiB | 94.0 s | 2268 ns/sec |
| silesia/x-ray | 8474240 | 3630236 | 42% | -6 | 1840 MiB | 30.6 s | 3607 ns/sec |
| silesia/xml | 5345280 | 307234 | 5% | -6 | 1841 MiB | 9.4 s | 1752 ns/sec |


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
| calgary/bib | 111261 | 22660 | 20% | -6 | 1840 MiB | 0.9 s | 8431 ns/sec |
| calgary/book1 | 768771 | 199198 | 25% | -6 | 1840 MiB | 2.4 s | 3101 ns/sec |
| calgary/book2 | 610856 | 128617 | 21% | -6 | 1840 MiB | 1.8 s | 3019 ns/sec |
| calgary/geo | 102400 | 45747 | 44% | -6 | 1840 MiB | 1.0 s | 9803 ns/sec |
| calgary/news | 377109 | 95455 | 25% | -6 | 1840 MiB | 1.5 s | 4109 ns/sec |
| calgary/obj1 | 21504 | 8818 | 41% | -6 | 1840 MiB | 0.6 s | 28007 ns/sec |
| calgary/obj2 | 246814 | 55894 | 22% | -6 | 1840 MiB | 1.4 s | 5591 ns/sec |
| calgary/paper1 | 53161 | 14159 | 26% | -6 | 1840 MiB | 0.6 s | 11821 ns/sec |
| calgary/paper2 | 82199 | 21686 | 26% | -6 | 1840 MiB | 0.7 s | 8788 ns/sec |
| calgary/paper3 | 46526 | 13858 | 29% | -6 | 1840 MiB | 0.7 s | 15515 ns/sec |
| calgary/paper4 | 13286 | 4444 | 33% | -6 | 1840 MiB | 0.6 s | 45422 ns/sec |
| calgary/paper5 | 11954 | 4226 | 35% | -6 | 1840 MiB | 0.6 s | 50474 ns/sec |
| calgary/paper6 | 38105 | 10405 | 27% | -6 | 1840 MiB | 0.7 s | 17690 ns/sec |
| calgary/pic | 513216 | 178120 | 34% | -6 | 1840 MiB | 1.2 s | 2245 ns/sec |
| calgary/progc | 39611 | 10531 | 26% | -6 | 1840 MiB | 0.6 s | 16311 ns/sec |
| calgary/progl | 71646 | 11907 | 16% | -6 | 1840 MiB | 0.7 s | 9402 ns/sec |
| calgary/progp | 49379 | 8409 | 17% | -6 | 1840 MiB | 0.7 s | 13492 ns/sec |
| calgary/trans | 93695 | 13191 | 14% | -6 | 1840 MiB | 0.8 s | 8624 ns/sec |


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
| canterbury/alice29.txt | 152089 | 36801 | 24% | -6 | 1840 MiB | 0.9 s | 5879 ns/sec |
| canterbury/alphabet.txt | 100000 | 160 | 0% | -6 | 1840 MiB | 0.5 s | 4942 ns/sec |
| canterbury/asyoulik.txt | 125179 | 34108 | 27% | -6 | 1840 MiB | 0.8 s | 6618 ns/sec |
| canterbury/bible.txt | 4047392 | 635886 | 15% | -6 | 1840 MiB | 8.0 s | 1967 ns/sec |
| canterbury/cp.html | 24603 | 6404 | 26% | -6 | 1840 MiB | 0.6 s | 23546 ns/sec |
| canterbury/e.coli | 4638690 | 1108694 | 23% | -6 | 1841 MiB | 10.8 s | 2338 ns/sec |
| canterbury/fields.c | 11150 | 2565 | 23% | -6 | 1840 MiB | 0.5 s | 47816 ns/sec |
| canterbury/grammar.lsp | 3721 | 1054 | 28% | -6 | 1840 MiB | 0.5 s | 130140 ns/sec |
| canterbury/kennedy.xls | 1029744 | 16794 | 1% | -6 | 1840 MiB | 2.4 s | 2351 ns/sec |
| canterbury/lcet10.txt | 426754 | 88859 | 20% | -6 | 1840 MiB | 1.3 s | 3075 ns/sec |
| canterbury/pi.txt | 1000000 | 415793 | 41% | -6 | 1841 MiB | 3.0 s | 2951 ns/sec |
| canterbury/plrabn12.txt | 481861 | 125850 | 26% | -6 | 1840 MiB | 1.6 s | 3367 ns/sec |
| canterbury/ptt5 | 513216 | 178120 | 34% | -6 | 1840 MiB | 1.1 s | 2226 ns/sec |
| canterbury/random.txt | 100000 | 75345 | 75% | -6 | 1840 MiB | 0.9 s | 8786 ns/sec |
| canterbury/sum | 38240 | 9561 | 25% | -6 | 1840 MiB | 0.6 s | 16388 ns/sec |
| canterbury/xargs.1 | 4227 | 1539 | 36% | -6 | 1840 MiB | 0.5 s | 112421 ns/sec |


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
