
#--------------------------------------------------------------------

ifeq ($(origin version), undefined)
	version = 0.1
endif

#--------------------------------------------------------------------

all: 3rd
	@( cd spcached; make )

3rd:
	@( cd spdict; make )
	@( cd spserver; make )

dist: clean spcached-$(version).src.tar.gz

spcached-$(version).src.tar.gz:
	@find . -type f | grep -v CVS | grep -v .svn | sed s:^./:spcached-$(version)/: > MANIFEST
	@(cd ..; ln -s spcached spcached-$(version))
	(cd ..; tar cvf - `cat spcached/MANIFEST` | gzip > spcached/spcached-$(version).src.tar.gz)
	@(cd ..; rm spcached-$(version))

clean:
	@( cd spdict; make clean )
	@( cd spserver; make clean )
	@( cd spcached; make clean )

