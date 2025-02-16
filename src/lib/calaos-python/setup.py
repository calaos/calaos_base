from setuptools import setup, find_packages

setup(
    name="calaos_extern_proc",
    version="1.0.0",
    packages=find_packages(),
    install_requires=[],
    author="Calaos",
    author_email="team@calaos.fr",
    description="External process library for Calaos",
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    url="https://github.com/calaos/calaos_base",
    classifiers=[
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: GPLv3 License",
        "Operating System :: OS Independent",
    ],
    python_requires=">=3.6",
)