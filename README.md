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

During the creation of the implementation, it was always assumed that the memory usage (option -6) should be less than 2 GiB with an acceptable speed (about 200 seconds or so for enwik8).

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


```bash
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio | Option | Memory used | Time used | Speed |
|:-----|:--------:|:----------:|:-----:|:------:|:-----------:|:---------:|:-----:|
| enwik8 | 100000000 | 18872012 | 18% | -0 | 87 MiB | 203.9 s | 2039 ns/byte |
| enwik8 | 100000000 | 18503169 | 18% | -1 | 116 MiB | 209.0 s | 2090 ns/byte |
| enwik8 | 100000000 | 18137901 | 18% | -2 | 174 MiB | 210.7 s | 2107 ns/byte |
| enwik8 | 100000000 | 17826884 | 17% | -3 | 290 MiB | 210.8 s | 2108 ns/byte |
| enwik8 | 100000000 | 17615475 | 17% | -4 | 523 MiB | 214.2 s | 2142 ns/byte |
| enwik8 | 100000000 | 17496204 | 17% | -5 | 988 MiB | 219.5 s | 2195 ns/byte |
| enwik8 | 100000000 | 17422112 | 17% | -6 | 1918 MiB | 226.4 s | 2264 ns/byte |
| enwik8 | 100000000 | 17409964 | 17% | -7 | 3777 MiB | 229.9 s | 2299 ns/byte |
| enwik8 | 100000000 | 17407769 | 17% | -8 | 7496 MiB | 232.4 s | 2324 ns/byte |
| enwik8 | 100000000 | 17405307 | 17% | -9 | 13909 MiB | 235.4 s | 2354 ns/byte |
| enwik9 | 1000000000 | 157373292 | 15% | -0 | 139 MiB | 1982.9 s | 1983 ns/byte |
| enwik9 | 1000000000 | 154042141 | 15% | -1 | 139 MiB | 2011.9 s | 2012 ns/byte |
| enwik9 | 1000000000 | 150475550 | 15% | -2 | 175 MiB | 2040.8 s | 2041 ns/byte |
| enwik9 | 1000000000 | 146642744 | 14% | -3 | 291 MiB | 2058.0 s | 2058 ns/byte |
| enwik9 | 1000000000 | 143091762 | 14% | -4 | 524 MiB | 2081.2 s | 2081 ns/byte |
| enwik9 | 1000000000 | 139941951 | 13% | -5 | 988 MiB | 2124.2 s | 2124 ns/byte |
| enwik9 | 1000000000 | 137772360 | 13% | -6 | 1918 MiB | 2189.6 s | 2190 ns/byte |
| enwik9 | 1000000000 | 136499538 | 13% | -7 | 3777 MiB | 2253.2 s | 2253 ns/byte |
| enwik9 | 1000000000 | 135972121 | 13% | -8 | 7497 MiB | 2286.5 s | 2287 ns/byte |
| enwik9 | 1000000000 | 135645203 | 13% | -9 | 13909 MiB | 2323.8 s | 2324 ns/byte |


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
| silesia/dickens | 10192446 | 2059068 | 20% | -6 | 1918 MiB | 21.3 s | 2089 ns/byte |
| silesia/mozilla | 51220480 | 10097246 | 19% | -6 | 1934 MiB | 192.5 s | 3759 ns/byte |
| silesia/mr | 9970564 | 2104612 | 21% | -6 | 1917 MiB | 23.7 s | 2379 ns/byte |
| silesia/nci | 33553445 | 964765 | 2% | -6 | 1918 MiB | 21.7 s | 645 ns/byte |
| silesia/ooffice | 6152192 | 1713607 | 27% | -6 | 1917 MiB | 23.1 s | 3756 ns/byte |
| silesia/osdb | 10085684 | 2144235 | 21% | -6 | 1917 MiB | 38.9 s | 3854 ns/byte |
| silesia/reymont | 6627202 | 860544 | 12% | -6 | 1934 MiB | 20.5 s | 3090 ns/byte |
| silesia/samba | 21606400 | 2217223 | 10% | -6 | 1933 MiB | 77.0 s | 3566 ns/byte |
| silesia/sao | 7251944 | 4360215 | 60% | -6 | 1917 MiB | 34.5 s | 4762 ns/byte |
| silesia/webster | 41458703 | 5438913 | 13% | -6 | 1918 MiB | 90.7 s | 2188 ns/byte |
| silesia/x-ray | 8474240 | 3627548 | 42% | -6 | 1917 MiB | 30.5 s | 3594 ns/byte |
| silesia/xml | 5345280 | 304932 | 5% | -6 | 1917 MiB | 9.2 s | 1730 ns/byte |


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
| calgary/bib | 111261 | 22525 | 20% | -6 | 1917 MiB | 0.8 s | 7365 ns/byte |
| calgary/book1 | 768771 | 198900 | 25% | -6 | 1917 MiB | 2.4 s | 3089 ns/byte |
| calgary/book2 | 610856 | 127866 | 20% | -6 | 1917 MiB | 1.9 s | 3045 ns/byte |
| calgary/geo | 102400 | 45635 | 44% | -6 | 1917 MiB | 0.9 s | 9035 ns/byte |
| calgary/news | 377109 | 94754 | 25% | -6 | 1917 MiB | 1.6 s | 4115 ns/byte |
| calgary/obj1 | 21504 | 8776 | 40% | -6 | 1917 MiB | 0.6 s | 25681 ns/byte |
| calgary/obj2 | 246814 | 54509 | 22% | -6 | 1917 MiB | 1.3 s | 5293 ns/byte |
| calgary/paper1 | 53161 | 14054 | 26% | -6 | 1917 MiB | 0.7 s | 12550 ns/byte |
| calgary/paper2 | 82199 | 21606 | 26% | -6 | 1917 MiB | 0.7 s | 8141 ns/byte |
| calgary/paper3 | 46526 | 13798 | 29% | -6 | 1917 MiB | 0.6 s | 13124 ns/byte |
| calgary/paper4 | 13286 | 4413 | 33% | -6 | 1917 MiB | 0.5 s | 40347 ns/byte |
| calgary/paper5 | 11954 | 4193 | 35% | -6 | 1917 MiB | 0.5 s | 45800 ns/byte |
| calgary/paper6 | 38105 | 10313 | 27% | -6 | 1917 MiB | 0.6 s | 15999 ns/byte |
| calgary/pic | 513216 | 44086 | 8% | -6 | 1917 MiB | 1.2 s | 2264 ns/byte |
| calgary/progc | 39611 | 10424 | 26% | -6 | 1917 MiB | 0.6 s | 15231 ns/byte |
| calgary/progl | 71646 | 11732 | 16% | -6 | 1917 MiB | 0.6 s | 8424 ns/byte |
| calgary/progp | 49379 | 8293 | 16% | -6 | 1917 MiB | 0.6 s | 12809 ns/byte |
| calgary/trans | 93695 | 12981 | 13% | -6 | 1917 MiB | 0.8 s | 8277 ns/byte |


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
| canterbury/alice29.txt | 152089 | 36686 | 24% | -6 | 1917 MiB | 0.9 s | 5835 ns/byte |
| canterbury/alphabet.txt | 100000 | 164 | 0% | -6 | 1917 MiB | 0.5 s | 4693 ns/byte |
| canterbury/asyoulik.txt | 125179 | 34001 | 27% | -6 | 1917 MiB | 0.8 s | 6522 ns/byte |
| canterbury/bible.txt | 4047392 | 634651 | 15% | -6 | 1917 MiB | 7.7 s | 1902 ns/byte |
| canterbury/cp.html | 24603 | 6316 | 25% | -6 | 1917 MiB | 0.5 s | 22012 ns/byte |
| canterbury/e.coli | 4638690 | 1108930 | 23% | -6 | 1918 MiB | 11.1 s | 2404 ns/byte |
| canterbury/fields.c | 11150 | 2528 | 22% | -6 | 1917 MiB | 0.5 s | 43157 ns/byte |
| canterbury/grammar.lsp | 3721 | 1039 | 27% | -6 | 1917 MiB | 0.5 s | 139307 ns/byte |
| canterbury/kennedy.xls | 1029744 | 16265 | 1% | -6 | 1917 MiB | 2.5 s | 2443 ns/byte |
| canterbury/lcet10.txt | 426754 | 88555 | 20% | -6 | 1917 MiB | 1.4 s | 3226 ns/byte |
| canterbury/pi.txt | 1000000 | 415681 | 41% | -6 | 1917 MiB | 2.7 s | 2716 ns/byte |
| canterbury/plrabn12.txt | 481861 | 125728 | 26% | -6 | 1917 MiB | 1.7 s | 3527 ns/byte |
| canterbury/ptt5 | 513216 | 44086 | 8% | -6 | 1917 MiB | 1.2 s | 2361 ns/byte |
| canterbury/random.txt | 100000 | 75340 | 75% | -6 | 1917 MiB | 0.9 s | 8812 ns/byte |
| canterbury/sum | 38240 | 9434 | 24% | -6 | 1917 MiB | 0.6 s | 16078 ns/byte |
| canterbury/xargs.1 | 4227 | 1519 | 35% | -6 | 1917 MiB | 0.5 s | 118198 ns/byte |


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
