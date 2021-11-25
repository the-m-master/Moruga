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
Moruga does also build using [cygwin](https://www.cygwin.com/), but is not recommended due to the DLL hell of cygwin. Currently Moruga does not build with VS, due to C++ compiler incompatibility (no C++20 support) and lack of 128-bit support.


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
| enwik8 | 100000000 | 18935794 | 18% | -0 | 73 MiB | 188.2 s | 1882 ns/sec |
| enwik8 | 100000000 | 18566070 | 18% | -1 | 101 MiB | 192.5 s | 1925 ns/sec |
| enwik8 | 100000000 | 18200762 | 18% | -2 | 157 MiB | 194.3 s | 1943 ns/sec |
| enwik8 | 100000000 | 17888851 | 17% | -3 | 269 MiB | 194.6 s | 1946 ns/sec |
| enwik8 | 100000000 | 17676683 | 17% | -4 | 494 MiB | 197.7 s | 1977 ns/sec |
| enwik8 | 100000000 | 17559203 | 17% | -5 | 943 MiB | 201.9 s | 2019 ns/sec |
| enwik8 | 100000000 | 17486266 | 17% | -6 | 1840 MiB | 209.1 s | 2091 ns/sec |
| enwik8 | 100000000 | 17477140 | 17% | -7 | 3636 MiB | 214.3 s | 2143 ns/sec |
| enwik8 | 100000000 | 17474766 | 17% | -8 | 7227 MiB | 213.7 s | 2137 ns/sec |
| enwik8 | 100000000 | 17474154 | 17% | -9 | 13383 MiB | 216.4 s | 2164 ns/sec |
| enwik9 | 1000000000 | 157965499 | 15% | -0 | 73 MiB | 1805.2 s | 1805 ns/sec |
| enwik9 | 1000000000 | 154628346 | 15% | -1 | 101 MiB | 1850.5 s | 1850 ns/sec |
| enwik9 | 1000000000 | 151059208 | 15% | -2 | 157 MiB | 1865.2 s | 1865 ns/sec |
| enwik9 | 1000000000 | 147209419 | 14% | -3 | 270 MiB | 1882.9 s | 1883 ns/sec |
| enwik9 | 1000000000 | 143652866 | 14% | -4 | 494 MiB | 1919.0 s | 1919 ns/sec |
| enwik9 | 1000000000 | 140702879 | 14% | -5 | 943 MiB | 1958.2 s | 1958 ns/sec |
| enwik9 | 1000000000 | 138365990 | 13% | -6 | 1841 MiB | 2007.0 s | 2007 ns/sec |
| enwik9 | 1000000000 | 137121001 | 13% | -7 | 3636 MiB | 2068.5 s | 2068 ns/sec |
| enwik9 | 1000000000 | 136598319 | 13% | -8 | 7227 MiB | 2084.1 s | 2084 ns/sec |
| enwik9 | 1000000000 | 136281285 | 13% | -9 | 13383 MiB | 2123.4 s | 2123 ns/sec |


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
| silesia/dickens | 10192446 | 2061055 | 20% | -6 | 1840 MiB | 21.5 s | 2109 ns/sec |
| silesia/mozilla | 51220480 | 10251437 | 20% | -6 | 1857 MiB | 194.1 s | 3789 ns/sec |
| silesia/mr | 9970564 | 2107003 | 21% | -6 | 1840 MiB | 23.0 s | 2305 ns/sec |
| silesia/nci | 33553445 | 970334 | 2% | -6 | 1841 MiB | 23.2 s | 691 ns/sec |
| silesia/ooffice | 6152192 | 1737908 | 28% | -6 | 1840 MiB | 23.5 s | 3819 ns/sec |
| silesia/osdb | 10085684 | 2153464 | 21% | -6 | 1840 MiB | 38.9 s | 3858 ns/sec |
| silesia/reymont | 6627202 | 862772 | 13% | -6 | 1857 MiB | 21.4 s | 3225 ns/sec |
| silesia/samba | 21606400 | 2243645 | 10% | -6 | 1856 MiB | 79.3 s | 3668 ns/sec |
| silesia/sao | 7251944 | 4362806 | 60% | -6 | 1840 MiB | 36.3 s | 5002 ns/sec |
| silesia/webster | 41458703 | 5453463 | 13% | -6 | 1841 MiB | 90.7 s | 2188 ns/sec |
| silesia/x-ray | 8474240 | 3629719 | 42% | -6 | 1840 MiB | 30.3 s | 3577 ns/sec |
| silesia/xml | 5345280 | 307182 | 5% | -6 | 1841 MiB | 9.0 s | 1681 ns/sec |


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
| calgary/bib | 111261 | 22651 | 20% | -6 | 1229 MiB | 1.0 s | 8985 ns/sec |
| calgary/book1 | 768771 | 199220 | 25% | -6 | 1338 MiB | 2.8 s | 3670 ns/sec |
| calgary/book2 | 610856 | 128595 | 21% | -6 | 1334 MiB | 2.2 s | 3544 ns/sec |
| calgary/geo | 102400 | 45727 | 44% | -6 | 1274 MiB | 1.0 s | 10243 ns/sec |
| calgary/news | 377109 | 95390 | 25% | -6 | 1325 MiB | 1.8 s | 4670 ns/sec |
| calgary/obj1 | 21504 | 8808 | 40% | -6 | 1127 MiB | 0.6 s | 27386 ns/sec |
| calgary/obj2 | 246814 | 55805 | 22% | -6 | 1305 MiB | 1.6 s | 6522 ns/sec |
| calgary/paper1 | 53161 | 14139 | 26% | -6 | 1176 MiB | 0.7 s | 13030 ns/sec |
| calgary/paper2 | 82199 | 21685 | 26% | -6 | 1214 MiB | 0.8 s | 9688 ns/sec |
| calgary/paper3 | 46526 | 13858 | 29% | -6 | 1186 MiB | 0.7 s | 14973 ns/sec |
| calgary/paper4 | 13286 | 4443 | 33% | -6 | 1113 MiB | 0.5 s | 40392 ns/sec |
| calgary/paper5 | 11954 | 4225 | 35% | -6 | 1109 MiB | 0.5 s | 45004 ns/sec |
| calgary/paper6 | 38105 | 10398 | 27% | -6 | 1154 MiB | 0.6 s | 16970 ns/sec |
| calgary/pic | 513216 | 44090 | 8% | -6 | 1274 MiB | 1.3 s | 2442 ns/sec |
| calgary/progc | 39611 | 10515 | 26% | -6 | 1161 MiB | 0.7 s | 17391 ns/sec |
| calgary/progl | 71646 | 11903 | 16% | -6 | 1168 MiB | 0.7 s | 9698 ns/sec |
| calgary/progp | 49379 | 8396 | 17% | -6 | 1147 MiB | 0.7 s | 13997 ns/sec |
| calgary/trans | 93695 | 13173 | 14% | -6 | 1174 MiB | 0.8 s | 8992 ns/sec |


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
| canterbury/alice29.txt | 152089 | 36793 | 24% | -6 | 1266 MiB | 1.1 s | 6958 ns/sec |
| canterbury/alphabet.txt | 100000 | 172 | 0% | -6 | 1073 MiB | 0.5 s | 4987 ns/sec |
| canterbury/asyoulik.txt | 125179 | 34096 | 27% | -6 | 1259 MiB | 1.0 s | 7691 ns/sec |
| canterbury/bible.txt | 4047392 | 635796 | 15% | -6 | 1365 MiB | 9.5 s | 2341 ns/sec |
| canterbury/cp.html | 24603 | 6403 | 26% | -6 | 1123 MiB | 0.6 s | 23923 ns/sec |
| canterbury/e.coli | 4638690 | 1108721 | 23% | -6 | 1383 MiB | 12.3 s | 2653 ns/sec |
| canterbury/fields.c | 11150 | 2568 | 23% | -6 | 1096 MiB | 0.5 s | 48067 ns/sec |
| canterbury/grammar.lsp | 3721 | 1052 | 28% | -6 | 1081 MiB | 0.5 s | 130942 ns/sec |
| canterbury/kennedy.xls | 1029744 | 16868 | 1% | -6 | 1350 MiB | 2.8 s | 2693 ns/sec |
| canterbury/lcet10.txt | 426754 | 88863 | 20% | -6 | 1324 MiB | 1.6 s | 3777 ns/sec |
| canterbury/pi.txt | 1000000 | 415797 | 41% | -6 | 1348 MiB | 3.4 s | 3432 ns/sec |
| canterbury/plrabn12.txt | 481861 | 125879 | 26% | -6 | 1333 MiB | 2.0 s | 4185 ns/sec |
| canterbury/ptt5 | 513216 | 44090 | 8% | -6 | 1274 MiB | 1.3 s | 2442 ns/sec |
| canterbury/random.txt | 100000 | 75341 | 75% | -6 | 1275 MiB | 1.0 s | 10481 ns/sec |
| canterbury/sum | 38240 | 9493 | 24% | -6 | 1155 MiB | 0.6 s | 16706 ns/sec |
| canterbury/xargs.1 | 4227 | 1536 | 36% | -6 | 1085 MiB | 0.5 s | 114026 ns/sec |


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
