/*
 * Copyright (C) 2016 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Thomas Voß <thomas.voss@canonical.com>
 *
 */
#ifndef BIOMETRY_UTIL_CLI_H_
#define BIOMETRY_UTIL_CLI_H_

#include <biometry/do_not_copy_or_move.h>
#include <biometry/optional.h>
#include <biometry/visibility.h>

#include <iomanip>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace biometry
{
namespace util
{
namespace cli
{
/// @brief ProgressBar prints a progress bar on the terminal.
///
/// Looks like:
/// Prefix[===============----------] xxx.xx %
///         |--------------- width ------------|
class BIOMETRY_DLL_PUBLIC ProgressBar
{
public:
    /// @brief ProgressBar initializes an instance with out, prefix and width.
    ProgressBar(std::ostream& out, const std::string& prefix = std::string{}, std::uint32_t width = 80);
    /// @brief ~ProgressBar cleans up and inserts a std::endl into out.
    ~ProgressBar();

    /// @brief update updates the terminal output for the given percentage.
    void update(double percentage);

private:
    /// @cond
    std::string prefix;
    std::uint32_t width;
    std::ostream& out;
    /// @endcond
};

template<std::size_t max>
class BIOMETRY_DLL_PUBLIC SizeConstrainedString
{
public:
    SizeConstrainedString(const std::string& s) : s{s}
    {
        if(s.size() > max)
            throw std::logic_error{"Max size exceeded " + std::to_string(max)};
    }

    const std::string& as_string() const
    {
        return s;
    }

    operator std::string() const
    {
        return s;
    }

private:
    std::string s;
};

template<std::size_t max>
BIOMETRY_DLL_PUBLIC bool operator<(const SizeConstrainedString<max>& lhs, const SizeConstrainedString<max>& rhs)
{
    return lhs.as_string() < rhs.as_string();
}

template<std::size_t max>
BIOMETRY_DLL_PUBLIC bool operator==(const SizeConstrainedString<max>& lhs, const SizeConstrainedString<max>& rhs)
{
    return lhs.as_string() == rhs.as_string();
}

template<std::size_t max>
BIOMETRY_DLL_PUBLIC std::ostream& operator<<(std::ostream& out, const SizeConstrainedString<max>& scs)
{
    return out << std::setw(max) << std::left << scs.as_string();
}

// We are imposing size constraints to ensure a consistent CLI layout.
typedef SizeConstrainedString<15> Name;
typedef SizeConstrainedString<60> Usage;
typedef SizeConstrainedString<60> Description;

/// @brief Flag models an input parameter to a command.
class BIOMETRY_DLL_PUBLIC Flag : public DoNotCopyOrMove
{
public:
    // Safe us some typing.
    typedef std::shared_ptr<Flag> Ptr;

    /// @brief notify announces a new value to the flag.
    virtual void notify(const std::string& value) = 0;
    /// @brief name returns the name of the Flag.
    const Name& name() const;
    /// @brief description returns a human-readable description of the flag.
    const Description& description() const;

protected:
    /// @brief Flag creates a new instance, initializing name and description
    /// from the given values.
    Flag(const Name& name, const Description& description);

private:
    Name name_;
    Description description_;
};

/// @brief TypedFlag implements Flag relying on operator<< and operator>> to read/write values to/from strings.
template<typename T>
class BIOMETRY_DLL_PUBLIC TypedFlag : public Flag
{
public:
    typedef std::shared_ptr<TypedFlag<T>> Ptr;

    TypedFlag(const Name& name, const Description& description) : Flag{name, description}
    {
    }

    /// @brief value installs the given value in the flag.
    TypedFlag& value(const T& value)
    {
        value_ = value;
        return *this;
    }

    /// @brief value returns the optional value associated with the flag.
    const Optional<T>& value() const
    {
        return value_;
    }

    /// @brief notify tries to unwrap a value of type T from value.
    void notify(const std::string& s) override
    {
        std::stringstream ss{s};
        T value; ss >> value;
        value_ = value;
    }

private:
    Optional<T> value_;
};

/// @brief TypedReferenceFlag implements Flag, relying on operator<</>> to convert to/from string representations,
/// updating the given mutable reference to a value of type T.
template<typename T>
class BIOMETRY_DLL_PUBLIC TypedReferenceFlag : public Flag
{
public:
    // Safe us some typing.
    typedef std::shared_ptr<TypedReferenceFlag<T>> Ptr;

    /// @brief TypedReferenceFlag initializes a new instance with name, description and value.
    TypedReferenceFlag(const Name& name, const Description& description, T& value)
        : Flag{name, description},
          value_{value}
    {
    }

    /// @brief notify tries to unwrap a value of type T from value,
    /// relying on operator>> to read from given string s.
    void notify(const std::string& s) override
    {
        std::stringstream ss{s};
        ss >> value_.get();
    }

private:
    std::reference_wrapper<T> value_;
};

/// @brief OptionalTypedReferenceFlag handles Optional<T> references, making sure that
/// a value is always read on notify, even if the Optional<T> wasn't initialized previously.
template<typename T>
class BIOMETRY_DLL_PUBLIC OptionalTypedReferenceFlag : public Flag
{
public:
    typedef std::shared_ptr<OptionalTypedReferenceFlag<T>> Ptr;

    OptionalTypedReferenceFlag(const Name& name, const Description& description, Optional<T>& value)
        : Flag{name, description},
          value_{value}
    {
    }

    /// @brief notify tries to unwrap a value of type T from value.
    void notify(const std::string& s) override
    {
        std::stringstream ss{s}; T value; ss >> value;
        value_.get() = value;
    }

private:
    std::reference_wrapper<Optional<T>> value_;
};

/// @brief Command abstracts an individual command available from the daemon.
class BIOMETRY_DLL_PUBLIC Command : public DoNotCopyOrMove
{
public:
    // Safe us some typing
    typedef std::shared_ptr<Command> Ptr;    

    /// @brief FlagsMissing is thrown if at least one required flag is missing.
    struct FlagsMissing : public std::runtime_error
    {
        /// @brief FlagsMissing initializes a new instance.
        FlagsMissing();
    };

    /// @brief FlagsWithWrongValue is thrown if a value passed on the command line is invalid.
    struct FlagsWithInvalidValue : public std::runtime_error
    {
        /// @brief FlagsWithInvalidValue initializes a new instance.
        FlagsWithInvalidValue();
    };

    /// @brief Context bundles information passed to Command::run invocations.
    struct Context
    {
        std::istream& cin;              ///< The std::istream that should be used for reading.
        std::ostream& cout;             ///< The std::ostream that should be used for writing.
        std::vector<std::string> args;  ///< The command line args.
    };

    /// @brief name returns the Name of the command.
    virtual Name name() const;

    /// @brief usage returns a short usage string for the command.
    virtual Usage usage() const;

    /// @brief description returns a longer string explaining the command.
    virtual Description description() const;

    /// @brief run puts the command to execution.
    virtual int run(const Context& context) = 0;

    /// @brief help prints information about a command to out.
    virtual void help(std::ostream& out) = 0;

protected:
    /// @brief Command initializes a new instance with the given name, usage and description.
    Command(const Name& name, const Usage& usage, const Description& description);

    /// @brief name adjusts the name of the command to n.
    // virtual void name(const Name& n);
    /// @brief usage adjusts the usage string of the comand to u.
    // virtual void usage(const Usage& u);
    /// @brief description adjusts the description string of the command to d.
    // virtual void description(const Description& d);

private:
    Name name_;
    Usage usage_;
    Description description_;
};

/// @brief CommandWithSubcommands implements Command, selecting one of a set of actions.
class BIOMETRY_DLL_PUBLIC CommandWithSubcommands : public Command
{
public:
    typedef std::shared_ptr<CommandWithSubcommands> Ptr;
    typedef std::function<int(const Context&)> Action;

    /// @brief CommandWithSubcommands initializes a new instance with the given name, usage and description
    CommandWithSubcommands(const Name& name, const Usage& usage, const Description& description);

    /// @brief command adds the given command to the set of known commands.
    CommandWithSubcommands& command(const Command::Ptr& command);

    /// @brief flag adds the given flag to the set of known flags.
    CommandWithSubcommands& flag(const Flag::Ptr& flag);

    // From Command
    int run(const Context& context) override;
    void help(std::ostream &out) override;

private:
    std::unordered_map<std::string, Command::Ptr> commands_;
    std::set<Flag::Ptr> flags_;
};

/// @brief CommandWithFlagsAction implements Command, executing an Action after handling
class BIOMETRY_DLL_PUBLIC CommandWithFlagsAndAction : public Command
{
public:
    typedef std::shared_ptr<CommandWithFlagsAndAction> Ptr;
    typedef std::function<int(const Context&)> Action;

    /// @brief CommandWithFlagsAndAction initializes a new instance with the given name, usage and description
    CommandWithFlagsAndAction(const Name& name, const Usage& usage, const Description& description);

    /// @brief flag adds the given flag to the set of known flags.
    CommandWithFlagsAndAction& flag(const Flag::Ptr& flag);

    /// @brief action installs the given action.
    CommandWithFlagsAndAction& action(const Action& action);

    // From Command
    int run(const Context& context) override;
    void help(std::ostream &out) override;

private:
    std::set<Flag::Ptr> flags_;
    Action action_;
};

namespace cmd
{
/// @brief HelpFor prints a help message for the given command on execution.
class Help : public Command
{
public:
    /// @brief HelpFor initializes a new instance with the given reference to a cmd.
    explicit Help(Command& cmd);

    // From Command
    int run(const Context &context) override;
    void help(std::ostream &out) override;

private:
    /// @cond
    Command& command;
    /// @endcond
};
}

/// @brief args returns a vector of strings assembled from argc and argv.
BIOMETRY_DLL_PUBLIC std::vector<std::string> args(int argc, char** argv);

/// @brief make_flag returns a flag with the given name and description.
template<typename T>
BIOMETRY_DLL_PUBLIC typename TypedFlag<T>::Ptr make_flag(const Name& name, const Description& description)
{
    return std::make_shared<TypedFlag<T>>(name, description);
}

/// @brief make_flag returns a flag with the given name and description, notifying updates to value.
template<typename T>
BIOMETRY_DLL_PUBLIC typename TypedReferenceFlag<T>::Ptr make_flag(const Name& name, const Description& desc, T& value)
{
    return std::make_shared<TypedReferenceFlag<T>>(name, desc, value);
}

/// @brief make_flag returns a flag with the given name and description, updating the given optional value.
template<typename T>
BIOMETRY_DLL_PUBLIC typename OptionalTypedReferenceFlag<T>::Ptr make_flag(const Name& name, const Description& desc, Optional<T>& value)
{
    return std::make_shared<OptionalTypedReferenceFlag<T>>(name, desc, value);
}
}
}
}

#endif // BIOMETRY_UTIL_CLI_H_
