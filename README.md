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
| enwik8 | 100000000 | 18824421 | 18% | -0 | 90 MiB | 204.5 s | 2045 ns/B |
| enwik8 | 100000000 | 18458287 | 18% | -1 | 119 MiB | 213.9 s | 2139 ns/B |
| enwik8 | 100000000 | 18097130 | 18% | -2 | 177 MiB | 209.9 s | 2099 ns/B |
| enwik8 | 100000000 | 17786829 | 17% | -3 | 294 MiB | 212.0 s | 2120 ns/B |
| enwik8 | 100000000 | 17576285 | 17% | -4 | 526 MiB | 215.0 s | 2150 ns/B |
| enwik8 | 100000000 | 17460336 | 17% | -5 | 991 MiB | 218.4 s | 2184 ns/B |
| enwik8 | 100000000 | 17387238 | 17% | -6 | 1921 MiB | 227.6 s | 2276 ns/B |
| enwik8 | 100000000 | 17374870 | 17% | -7 | 3780 MiB | 231.0 s | 2310 ns/B |
| enwik8 | 100000000 | 17369761 | 17% | -8 | 7500 MiB | 234.5 s | 2345 ns/B |
| enwik8 | 100000000 | 17367527 | 17% | -9 | 13912 MiB | 239.1 s | 2391 ns/B |
| enwik8 | 100000000 | 17367164 | 17% | -10 | 25711 MiB | 247.1 s | 2471 ns/B |
| enwik9 | 1000000000 | 156759894 | 15% | -0 | 139 MiB | 1989.4 s | 1989 ns/B |
| enwik9 | 1000000000 | 153454605 | 15% | -1 | 139 MiB | 2021.1 s | 2021 ns/B |
| enwik9 | 1000000000 | 149780981 | 14% | -2 | 178 MiB | 2032.2 s | 2032 ns/B |
| enwik9 | 1000000000 | 145982398 | 14% | -3 | 294 MiB | 2058.0 s | 2058 ns/B |
| enwik9 | 1000000000 | 142453545 | 14% | -4 | 527 MiB | 2077.6 s | 2078 ns/B |
| enwik9 | 1000000000 | 139511579 | 13% | -5 | 991 MiB | 2124.5 s | 2124 ns/B |
| enwik9 | 1000000000 | 137352652 | 13% | -6 | 1921 MiB | 2189.2 s | 2189 ns/B |
| enwik9 | 1000000000 | 136069746 | 13% | -7 | 3781 MiB | 2256.4 s | 2256 ns/B |
| enwik9 | 1000000000 | 135525625 | 13% | -8 | 7501 MiB | 2300.1 s | 2300 ns/B |
| enwik9 | 1000000000 | 135200603 | 13% | -9 | 13913 MiB | 2325.5 s | 2326 ns/B |
| enwik9 | 1000000000 | 135171023 | 13% | -10 | 25712 MiB | 2343.5 s | 2343 ns/B |


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
| silesia/dickens | 10192446 | 2058795 | 20% | -6 | 1921 MiB | 21.6 s | 2120 ns/B |
| silesia/mozilla | 51220480 | 10139290 | 19% | -6 | 1938 MiB | 191.1 s | 3731 ns/B |
| silesia/mr | 9970564 | 2102552 | 21% | -6 | 1921 MiB | 22.8 s | 2290 ns/B |
| silesia/nci | 33553445 | 956074 | 2% | -6 | 1921 MiB | 21.8 s | 649 ns/B |
| silesia/ooffice | 6152192 | 1718948 | 27% | -6 | 1921 MiB | 23.4 s | 3810 ns/B |
| silesia/osdb | 10085684 | 2147976 | 21% | -6 | 1921 MiB | 39.5 s | 3917 ns/B |
| silesia/reymont | 6627202 | 859819 | 12% | -6 | 1937 MiB | 21.1 s | 3178 ns/B |
| silesia/samba | 21606400 | 2211424 | 10% | -6 | 1937 MiB | 76.5 s | 3541 ns/B |
| silesia/sao | 7251944 | 4369892 | 60% | -6 | 1921 MiB | 34.1 s | 4703 ns/B |
| silesia/webster | 41458703 | 5420872 | 13% | -6 | 1921 MiB | 91.9 s | 2216 ns/B |
| silesia/x-ray | 8474240 | 3626216 | 42% | -6 | 1920 MiB | 29.7 s | 3504 ns/B |
| silesia/xml | 5345280 | 304501 | 5% | -6 | 1921 MiB | 9.4 s | 1752 ns/B |


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
| calgary/bib | 111261 | 22475 | 20% | -6 | 1921 MiB | 0.9 s | 8306 ns/B |
| calgary/book1 | 768771 | 198884 | 25% | -6 | 1921 MiB | 2.5 s | 3294 ns/B |
| calgary/book2 | 610856 | 127868 | 20% | -6 | 1921 MiB | 1.9 s | 3179 ns/B |
| calgary/geo | 102400 | 45648 | 44% | -6 | 1921 MiB | 1.1 s | 10380 ns/B |
| calgary/news | 377109 | 94625 | 25% | -6 | 1921 MiB | 1.6 s | 4367 ns/B |
| calgary/obj1 | 21504 | 8791 | 40% | -6 | 1921 MiB | 0.5 s | 22822 ns/B |
| calgary/obj2 | 246814 | 54835 | 22% | -6 | 1921 MiB | 1.5 s | 6156 ns/B |
| calgary/paper1 | 53161 | 14100 | 26% | -6 | 1920 MiB | 0.7 s | 13336 ns/B |
| calgary/paper2 | 82199 | 21649 | 26% | -6 | 1921 MiB | 0.8 s | 9634 ns/B |
| calgary/paper3 | 46526 | 13773 | 29% | -6 | 1921 MiB | 0.7 s | 14446 ns/B |
| calgary/paper4 | 13286 | 4419 | 33% | -6 | 1921 MiB | 0.4 s | 32009 ns/B |
| calgary/paper5 | 11954 | 4179 | 34% | -6 | 1920 MiB | 0.4 s | 34604 ns/B |
| calgary/paper6 | 38105 | 10371 | 27% | -6 | 1921 MiB | 0.6 s | 15403 ns/B |
| calgary/pic | 513216 | 43945 | 8% | -6 | 1921 MiB | 1.2 s | 2391 ns/B |
| calgary/progc | 39611 | 10373 | 26% | -6 | 1921 MiB | 0.6 s | 14811 ns/B |
| calgary/progl | 71646 | 11763 | 16% | -6 | 1921 MiB | 0.6 s | 9055 ns/B |
| calgary/progp | 49379 | 8256 | 16% | -6 | 1921 MiB | 0.6 s | 11328 ns/B |
| calgary/trans | 93695 | 13033 | 13% | -6 | 1920 MiB | 0.7 s | 7578 ns/B |


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
| canterbury/alice29.txt | 152089 | 36700 | 24% | -6 | 1921 MiB | 1.0 s | 6256 ns/B |
| canterbury/alphabet.txt | 100000 | 167 | 0% | -6 | 1921 MiB | 0.4 s | 3578 ns/B |
| canterbury/asyoulik.txt | 125179 | 34033 | 27% | -6 | 1921 MiB | 1.0 s | 7675 ns/B |
| canterbury/bible.txt | 4047392 | 634106 | 15% | -6 | 1921 MiB | 8.0 s | 1965 ns/B |
| canterbury/cp.html | 24603 | 6321 | 25% | -6 | 1921 MiB | 0.5 s | 20116 ns/B |
| canterbury/e.coli | 4638690 | 1108599 | 23% | -6 | 1921 MiB | 11.0 s | 2366 ns/B |
| canterbury/fields.c | 11150 | 2530 | 22% | -6 | 1920 MiB | 0.4 s | 37254 ns/B |
| canterbury/grammar.lsp | 3721 | 1051 | 28% | -6 | 1920 MiB | 0.3 s | 90456 ns/B |
| canterbury/kennedy.xls | 1029744 | 15099 | 1% | -6 | 1920 MiB | 2.7 s | 2580 ns/B |
| canterbury/lcet10.txt | 426754 | 88642 | 20% | -6 | 1920 MiB | 1.5 s | 3564 ns/B |
| canterbury/pi.txt | 1000000 | 415679 | 41% | -6 | 1920 MiB | 2.7 s | 2733 ns/B |
| canterbury/plrabn12.txt | 481861 | 125714 | 26% | -6 | 1921 MiB | 1.8 s | 3836 ns/B |
| canterbury/ptt5 | 513216 | 43945 | 8% | -6 | 1921 MiB | 1.2 s | 2423 ns/B |
| canterbury/random.txt | 100000 | 75335 | 75% | -6 | 1920 MiB | 1.0 s | 10327 ns/B |
| canterbury/sum | 38240 | 9429 | 24% | -6 | 1921 MiB | 0.6 s | 14746 ns/B |
| canterbury/xargs.1 | 4227 | 1524 | 36% | -6 | 1920 MiB | 0.4 s | 85738 ns/B |


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
Moruga c <option> <infile> <outfile>
```

| File | Original | Compressed | Ratio |
|:-----|:--------:|:----------:|:-----:|
| misguided/enwik8.drt | 60824424 | 17301537 | 28% |
| misguided/enwik9.drt | 639386659 | 138164121 | 21% |
| misguided/ready4cmix | 934188796 | 131787238 | 14% |


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
