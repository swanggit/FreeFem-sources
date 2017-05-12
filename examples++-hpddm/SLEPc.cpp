//ff-c++-LIBRARY-dep: cxx11 hpddm slepc mpi
//ff-c++-cpp-dep: 

#include "slepc.h"

#include "PETSc.hpp"

namespace SLEPc {
template<class K, typename std::enable_if<std::is_same<K, double>::value || !std::is_same<PetscScalar, double>::value>::type* = nullptr>
void copy(K* pt, PetscInt n, PetscScalar* xr, PetscScalar* xi) { }
template<class K, typename std::enable_if<!std::is_same<K, double>::value && std::is_same<PetscScalar, double>::value>::type* = nullptr>
void copy(K* pt, PetscInt n, PetscScalar* xr, PetscScalar* xi) {
    for(int i = 0; i < n; ++i)
        pt[i] = K(xr[i], xi[i]);
}
template<class K, typename std::enable_if<std::is_same<K, double>::value || !std::is_same<PetscScalar, double>::value>::type* = nullptr>
void assign(K* pt, PetscScalar& kr, PetscScalar& ki) {
    *pt = kr;
}
template<class K, typename std::enable_if<!std::is_same<K, double>::value && std::is_same<PetscScalar, double>::value>::type* = nullptr>
void assign(K* pt, PetscScalar& kr, PetscScalar& ki) {
    *pt = K(kr, ki);
}
template<class Type, class K>
class eigensolver_Op : public E_F0mps {
    public:
        Expression A;
        Expression B;
        static const int n_name_param = 4;
        static basicAC_F0::name_and_type name_param[];
        Expression nargs[n_name_param];
        eigensolver_Op(const basicAC_F0& args, Expression param1, Expression param2) : A(param1), B(param2) {
            args.SetNameParam(n_name_param, name_param, nargs);
        }

        AnyType operator()(Stack stack) const;
};
template<class Type, class K>
basicAC_F0::name_and_type eigensolver_Op<Type, K>::name_param[] = {
    {"sparams", &typeid(std::string*)},
    {"prefix", &typeid(std::string*)},
    {"values", &typeid(KN<K>*)},
    {"vectors", &typeid(FEbaseArrayKn<K>*)}
};
template<class Type, class K>
class eigensolver : public OneOperator {
    public:
        K dummy;
        eigensolver() : OneOperator(atype<long>(), atype<Type*>(), atype<Type*>()) { }

        E_F0* code(const basicAC_F0& args) const {
            return new eigensolver_Op<Type, K>(args, t[0]->CastTo(args[0]), t[1]->CastTo(args[1]));
        }
};
template<class Type, class K>
AnyType eigensolver_Op<Type, K>::operator()(Stack stack) const {
    Type* ptA = GetAny<Type*>((*A)(stack));
    Type* ptB = GetAny<Type*>((*B)(stack));
    if(ptA->_petsc) {
        EPS eps;
        EPSCreate(PETSC_COMM_WORLD, &eps);
        EPSSetOperators(eps, ptA->_petsc, ptB->_A ? ptB->_petsc : NULL);
        std::string* options = nargs[0] ? GetAny<std::string*>((*nargs[0])(stack)) : NULL;
        if(options) {
            std::vector<std::string> elems;
            std::stringstream ss(*options);
            std::string item;
            while (std::getline(ss, item, ' '))
                elems.push_back(item);
            int argc = elems.size() + 1;
            char** data = new char*[argc];
            data[0] = new char[options->size() + argc]();
            data[1] = data[0] + 1;
            for(int i = 0; i < argc - 1; ++i) {
                if(i > 0)
                    data[i + 1] = data[i] + elems[i - 1].size() + 1;
                strcpy(data[i + 1], elems[i].c_str());
            }
            FFPetscOptionsInsert(&argc, &data, NULL);
            delete [] *data;
            delete [] data;
        }
        if(nargs[1])
            EPSSetOptionsPrefix(eps, GetAny<std::string*>((*nargs[1])(stack))->c_str());
        EPSSetFromOptions(eps);
        EPSSetUp(eps);
        EPSSolve(eps);
        PetscInt nconv;
        EPSGetConverged(eps, &nconv);
        if(nconv > 0 && (nargs[2] || nargs[3])) {
            KN<K>* eigenvalues = nargs[2] ? GetAny<KN<K>* >((*nargs[2])(stack)) : nullptr;
            FEbaseArrayKn<K>* eigenvectors = nargs[3] ? GetAny<FEbaseArrayKn<K>* >((*nargs[3])(stack)) : nullptr;
            if(eigenvalues)
                eigenvalues->resize(nconv);
            if(eigenvectors)
                eigenvectors->resize(nconv);
            Vec xr, xi;
            PetscInt n;
            if(eigenvectors) {
                MatCreateVecs(ptA->_petsc, PETSC_NULL, &xr);
                MatCreateVecs(ptA->_petsc, PETSC_NULL, &xi);
                VecGetLocalSize(xr, &n);
            }
            for(PetscInt i = 0; i < nconv; ++i) {
                PetscScalar kr, ki;
                EPSGetEigenpair(eps, i, &kr, &ki, eigenvectors ? xr : NULL, eigenvectors && std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value ? xi : NULL);
                if(eigenvectors) {
                    PetscScalar* tmpr;
                    PetscScalar* tmpi;
                    VecGetArray(xr, &tmpr);
                    K* array;
                    if(std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value) {
                        VecGetArray(xi, &tmpi);
                        array = new K[n];
                        copy(array, n, tmpr, tmpi);
                    }
                    else
                        array = reinterpret_cast<K*>(tmpr);
                    KN<K> cpy(eigenvectors->get(0)->n);
                    cpy = K(0.0);
                    HPDDM::Subdomain<K>::template distributedVec<1>(ptA->_num, ptA->_first, ptA->_last, static_cast<K*>(cpy), array, cpy.n, 1);
                    ptA->_A->HPDDM::template Subdomain<PetscScalar>::exchange(static_cast<K*>(cpy));
                    eigenvectors->set(i, cpy);
                    if(std::is_same<PetscScalar, double>::value && std::is_same<K, std::complex<double>>::value)
                        delete [] array;
                    else
                        VecRestoreArray(xi, &tmpi);
                    VecRestoreArray(xr, &tmpr);
                }
                if(eigenvalues) {
                    if(sizeof(PetscScalar) == sizeof(K))
                        (*eigenvalues)[i] = kr;
                    else
                        assign(static_cast<K*>(*eigenvalues + i), kr, ki);

                }
            }
            if(eigenvectors) {
                VecDestroy(&xr);
                VecDestroy(&xi);
            }
        }
        EPSDestroy(&eps);
        return static_cast<long>(nconv);
    }
    else
        return 0L;
}
void finalizeSLEPc() {
    PETSC_COMM_WORLD = MPI_COMM_WORLD;
    SlepcFinalize();
}
template<class K, typename std::enable_if<std::is_same<K, double>::value>::type* = nullptr>
void addSLEPc() {
    Global.Add("deigensolver", "(", new SLEPc::eigensolver<PETSc::DistributedCSR<HpSchwarz<PetscScalar>>, double>());
}
template<class K, typename std::enable_if<!std::is_same<K, double>::value>::type* = nullptr>
void addSLEPc() { }
}

static void Init_SLEPc() {
    int argc = pkarg->n;
    char** argv = new char*[argc];
    for(int i = 0; i < argc; ++i)
        argv[i] = const_cast<char*>((*(*pkarg)[i].getap())->c_str());
    PetscBool isInitialized;
    PetscInitialized(&isInitialized);
    if(!isInitialized && mpirank == 0)
        std::cout << "PetscInitialize has not been called, do not forget to load PETSc before loading SLEPc" << std::endl;
    SlepcInitialize(&argc, &argv, 0, "");
    delete [] argv;
    ff_atend(SLEPc::finalizeSLEPc);
    SLEPc::addSLEPc<PetscScalar>();
    Global.Add("zeigensolver", "(", new SLEPc::eigensolver<PETSc::DistributedCSR<HpSchwarz<PetscScalar>>, std::complex<double>>());
}

LOADFUNC(Init_SLEPc)
