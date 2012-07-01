all: doxygen iocBootDocs install

doxygen:
	doxygen

iocBootDocs:
	./makeIOCBootDocs

install:
	cp -f *.html /home/epics/web_software/epics
	cp -f *.png /home/epics/web_software/epics
	cp -rfp quadEMDoxygenHTML  /home/epics/web_software/epics/

clean:
	rm -rf quadEMDoxygenHTML/*


realclean: clean
