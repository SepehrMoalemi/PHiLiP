# Ubuntu minimal installation VirtualBox
### Download necessary packages
sudo apt install -y \
	vim \
	git \
	build-essential \
	libopenmpi-dev \
	libopenblas-dev \
	liblua5.3-dev \
	zlib1g-dev \
	python2 \
	python3 \
	cmake \
	libboost-dev \
	libmetis-dev \
	libgmsh-dev \
	doxygen \
	graphviz \
	texlive \
	liboce-ocaf-lite-dev
	


### Add some paths to ~/.bashrc
echo 'export OMP_NUM_THREADS=1
export Codes=$HOME/Codes
export Libraries=$HOME/Libraries
export DEAL_II_DIR=$Libraries/dealii/install
export TRILINOS_DIR=$Libraries/Trilinos/install
export P4EST_DIR=$Libraries/p4est-install
export PETSC_DIR=$Libraries/petsc
export PETSC_ARCH=ubuntu_openmpi_openblas' >> ~/.bashrc
# Load new .bashrc
source ~/.bashrc

# OPTIONAL My personal vim preferences
git clone https://github.com/dougshidong/.vim.git
(cd .vim && git submodule init && git submodule update)

### LIBRARY SETUP

## Setup Libraries directories
cd
mkdir -p Libraries
mkdir -p Codes

## Git clone libraries
(cd "$Libraries"; \
	git clone https://github.com/dougshidong/Trilinos.git; \
	git clone https://gitlab.com/petsc/petsc.git; \
	git clone https://github.com/cburstedde/p4est.git; \
	git clone https://github.com/dougshidong/dealii.git
)

 
## TRILINOS
(cd $Libraries/Trilinos; \
	# Add upstream original developer
	# Note that the fork dougshidong/Trilinos is kept up-to-date such that PHiLIP works properly
	git remote add upstream https://github.com/trilinos/Trilinos.git; \
	git checkout develop; \
	mkdir -p build && cd build; \
	# Setup the cmake
	wget https://raw.githubusercontent.com/dougshidong/PHiLiP/master/doc/prep_trilinos.sh; \
	sh prep_trilinos.sh; \
	## Download script prep_trilinos.sh contains
	# cmake \
	# -D BLAS_LIBRARY_NAMES='openblas' \
	# -D LAPACK_LIBRARY_NAMES='openblas' \
	# -D Trilinos_ENABLE_Amesos=ON \
	# -D Trilinos_ENABLE_Epetra=ON \
	# -D Trilinos_ENABLE_EpetraExt=ON \
	# -D Trilinos_ENABLE_Ifpack=ON \
	# -D Trilinos_ENABLE_AztecOO=ON \
	# -D Trilinos_ENABLE_Sacado=ON \
	# -D Trilinos_ENABLE_Teuchos=ON \
	# -D Trilinos_ENABLE_MueLu=ON \
	# -D Trilinos_ENABLE_ML=ON \
	# -D Trilinos_ENABLE_ROL=ON \
	# -D Trilinos_ENABLE_Tpetra=ON \
	# -D Trilinos_ENABLE_COMPLEX_DOUBLE=ON \
	# -D Trilinos_ENABLE_COMPLEX_FLOAT=ON \
	# -D Trilinos_ENABLE_Zoltan=ON \
	# -D Trilinos_VERBOSE_CONFIGURE=OFF \
	# -D TPL_ENABLE_MPI=ON \
	# -D BUILD_SHARED_LIBS=ON \
	# -D CMAKE_VERBOSE_MAKEFILE=OFF \
	# -D CMAKE_BUILD_TYPE=RELEASE \
	# -D CMAKE_INSTALL_PREFIX=$TRILINOS_DIR \
	# ..\
	make -j8; \
	make install ;\
)

## PETSc
(cd $Libraries/petsc; \
	# Setup petsc
	./configure --with-cc=mpicc --with-cxx=mpicxx --with-fc=mpif90 --with-debugging=0 COPTFLAGS='-O3 -march=native -mtune=native' CXXOPTFLAGS='-O3 -march=native -mtune=native' FOPTFLAGS='-O3 -march=native -mtune=native' ; \
	# Build and install
	make PETSC_DIR=$Libraries/petsc PETSC_ARCH=ubuntu_openmpi_openblas all ;\
	make PETSC_DIR=$Libraries/petsc PETSC_ARCH=ubuntu_openmpi_openblas check ;\
)


## Install p4est from source
(cd $Libraries/p4est; \
	# Setup p4est
	git submodule init && git submodule update; \
	./bootstrap;\
	./configure --enable-mpi --enable-shared \
			--disable-vtk-binary --without-blas --disable-mpithread \
			--prefix="$P4EST_DIR" CFLAGS="-O2" \
			CPPFLAGS="-DSC_LOG_PRIORITY=SC_LP_ESSENTIAL -DP4EST_BACKWARD_DEALII" ; \
	# Build and install
	make -C sc -j 8 > make.output ;\
	make -j8 ;\
	make install ;\
)

## deal.II
(cd $Libraries/dealii ;\
	# Add upstream original developer
	# Note that the fork dougshidong/dealii is kept up-to-date such that PHiLIP works properly
	git remote add upstream https://github.com/dealii/dealii.git ;\
	mkdir -p build && cd build ;\
	## Download script that contains:
	# cmake \
	# ../ \
	# -DCMAKE_INSTALL_PREFIX="$DEAL_II_DIR" \
	# -DDEAL_II_COMPONENT_DOCUMENTATION=ON \
	# -DDEAL_II_WITH_MPI=ON \
	# -DCMAKE_CXX_COMPILER=mpicxx \
	# -DCMAKE_CXX_GLAFS=-march=native \
	# -DCMAKE_C_COMPILER=mpicc \
	# -DCMAKE_Fortran_COMPILER= \
	# -DDEAL_II_ALLOW_BUNDLED=ON \
	# -DDEAL_II_WITH_GMSH=ON \
	# -DDEAL_II_WITH_CXX17=ON \
	# -DDEAL_II_WITH_OPENCASCADE=ON \
	# -DDEAL_II_WITH_METIS=ON \
	# -DDEAL_II_WITH_TRILINOS=ON \
	# -DDEAL_II_WITH_P4EST=ON \
	wget https://raw.githubusercontent.com/dougshidong/PHiLiP/master/doc/install_dealii.sh ;\
	sh install_dealii.sh ;\
	make -j8 ;\
	make install ;\
)

## PHiLiP
(cd $Codes ;\
	git clone https://github.com/dougshidong/PHiLiP.git ;\
	cd $Codes/PHiLiP ;\
	# Release build with all the optimization flags
	mkdir -p build_release && cd build_release ;\
	# MPI_MAX is the number of cores to use by default for tests with MPI
	# USE_LD_GOLD uses the ld.gold linker, which is much faster than the default ld linker
	# 	however, it does not work well on Ubuntu with OpenMPI. Works well with Fedora
	cmake ../ -DCMAKE_BUILD_TYPE=Release -DMPI_MAX=4 -DUSE_LD_GOLD=OFF ;\
	make -j8 ;\
	ctest ;\
)
